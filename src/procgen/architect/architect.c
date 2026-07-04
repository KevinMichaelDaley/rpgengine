/**
 * @file architect.c
 * @brief Architect VLM implementation: prompt building + VLM call + reprompting.
 */

#include "ferrum/procgen/procgen_architect.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/engine_settings.h"
#include "ferrum/llm/llm_cost_tracker.h"

#include <curl/curl.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Response buffer for curl ─────────────────────────────────── */

typedef struct {
    char  *data;
    size_t size;
} rb_t;

static size_t write_cb(void *ptr, size_t sz, size_t nmemb, void *user) {
    size_t total = sz * nmemb;
    rb_t *rb = (rb_t *)user;
    char *nd = realloc(rb->data, rb->size + total + 1);
    if (!nd) return 0;
    rb->data = nd;
    memcpy(rb->data + rb->size, ptr, total);
    rb->size += total;
    rb->data[rb->size] = '\0';
    return total;
}

/* ── Minimal JSON extraction ──────────────────────────────────── */

static const char *find_key(const char *json, const char *key) {
    size_t kl = strlen(key);
    const char *p = json;
    while ((p = strstr(p, key))) {
        if (p > json && p[-1] == '"' && p[kl] == '"' && p[kl+1] == ':') {
            const char *v = p + kl + 2;
            while (*v == ' ' || *v == '\t' || *v == '\n') v++;
            if (*v == '"') return v + 1;
            return v;
        }
        p += kl;
    }
    return NULL;
}

static void extract_str(const char *s, char *out, size_t cap) {
    size_t i = 0;
    while (s && *s && *s != '"' && i < cap - 1) out[i++] = *s++;
    out[i] = '\0';
}

static int extract_int(const char *s) { return atoi(s); }

/* ── Default system prompt ────────────────────────────────────── */

static const char *DEFAULT_SYSTEM_PROMPT =
    "You are a dungeon architect for a survival/crafting RPG. "
    "You generate level layouts using the following shape grammar. "
    "Output ONLY valid grammar tokens — no explanation, no markdown, no code blocks.\n\n"

    "GRAMMAR: blockout v1\n\n"

    "Tokens:\n"
    "  ROOM_QUAD   x=<float> y=<float> w=<width> h=<depth> floor_z=<float> ceil_z=<float> [name=<string>]\n"
    "    → Creates a rectangular room. Example: ROOM_QUAD x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6 name=entrance\n"
    "  ROOM_PENT   floor_z=<float> ceil_z=<float> [name=<string>]\n"
    "    → Creates a 5-sided room.\n"
    "  CORRIDOR_H  from=(x1,y1) to=(x2,y2) w=<width> floor_z=<float> ceil_z=<float>\n"
    "    → Horizontal corridor. y values must match.\n"
    "  CORRIDOR_V  from=(x1,y1) to=(x2,y2) w=<width> floor_z=<float> ceil_z=<float>\n"
    "    → Vertical corridor. x values must match.\n"
    "  CORRIDOR_DIAG  from=(x1,y1) to=(x2,y2) w=<width> floor_z=<float> ceil_z=<float>\n"
    "    → Diagonal corridor.\n"
    "  DOOR   at=(x,y) [w=<width> h=<height>]\n"
    "  WINDOW at=(x,y) [w=<width> h=<height>]\n"
    "  RAMP_UP    from=(x1,y1) to=(x2,y2) dz=<height_change> [w=<width>]\n"
    "  RAMP_DOWN  from=(x1,y1) to=(x2,y2) dz=<height_change> [w=<width>]\n"
    "  SPAWN   x=<float> y=<float> z=<float>\n"
    "    → Player start position. REQUIRED — exactly one per level.\n"
    "  MARKER  x=<float> y=<float> z=<float> name=<string>\n"
    "    → Named waypoint. REQUIRED — at least 3 with distinct names.\n"
    "  BLOCK ... EBLOCK\n"
    "    → Groups tokens for multi-floor nesting.\n\n"

    "RULES:\n"
    "1. Begin with: @grammar blockout v1\n"
    "2. Exactly one SPAWN token.\n"
    "3. At least 3 MARKER tokens with distinct descriptive names.\n"
    "4. Rooms must not overlap. All rooms must have ceil_z > floor_z ≥ 0.\n"
    "5. Corridors connect rooms. Place DOOR tokens at corridor endpoints.\n"
    "6. Corridor CORRIDOR_H must have same y in from and to.\n"
    "   Corridor CORRIDOR_V must have same x in from and to.\n"
    "7. Coordinates are in world-space meters.\n"
    "8. Use RAMPs to change floor height between rooms.\n"
    "9. Output ONLY valid grammar tokens. No commentary, no markdown.\n\n"

    "USER REQUEST: ";

/* ── Build system prompt ──────────────────────────────────────── */

static int build_prompt(const char *grammar_name,
                        const char *user_request,
                        const char *grammar_prompt,
                        const char *error_context,
                        char *out, size_t out_cap) {
    const char *sys = grammar_prompt ? grammar_prompt : DEFAULT_SYSTEM_PROMPT;
    int written;

    if (error_context && error_context[0]) {
        written = snprintf(out, out_cap,
            "@grammar %s v1\n\n"
            "%s\n\n"
            "USER REQUEST: %s\n\n"
            "PREVIOUS ATTEMPT FAILED: %s\n"
            "Please correct the errors and output ONLY valid grammar tokens.",
            grammar_name, sys, user_request, error_context);
    } else {
        written = snprintf(out, out_cap,
            "@grammar %s v1\n\n"
            "%s\n\n"
            "USER REQUEST: %s\n\n"
            "Output ONLY valid grammar tokens. No explanation.",
            grammar_name, sys, user_request);
    }

    return (written > 0 && (size_t)written < out_cap) ? 0 : -1;
}

/* ── Call VLM ─────────────────────────────────────────────────── */

static int call_vlm(const char *user_prompt,
                    char *response, size_t resp_cap,
                    int *in_tok, int *out_tok) {
    const fr_engine_settings_t *cfg = fr_engine_settings_get();
    if (!cfg) {
        snprintf(response, resp_cap, "engine settings not initialized");
        return -1;
    }

    if (cfg->llm_base_url[0] == '\0' || cfg->llm_model[0] == '\0') {
        snprintf(response, resp_cap,
                 "LLM not configured: set llm_base_url and llm_model in engine settings");
        return -1;
    }

    /* Build JSON payload — system message only (no separate system role for simplicity). */
    char payload[65536];
    int plen = snprintf(payload, sizeof(payload),
        "{"
        "\"model\":\"%s\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"max_tokens\":%u,"
        "\"temperature\":0.0"
        "}",
        cfg->llm_model, user_prompt, cfg->llm_max_tokens > 0 ? cfg->llm_max_tokens : 4096);

    if (plen < 0 || (size_t)plen >= sizeof(payload)) {
        snprintf(response, resp_cap, "prompt too large");
        return -1;
    }

    /* HTTP POST. */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        snprintf(response, resp_cap, "curl init failed");
        return -1;
    }

    char url[512];
    snprintf(url, sizeof(url), "%s/chat/completions", cfg->llm_base_url);

    rb_t rb = {0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");
    if (cfg->llm_api_key[0]) {
        char auth[512];
        snprintf(auth, sizeof(auth), "Authorization: Bearer %s", cfg->llm_api_key);
        headers = curl_slist_append(headers, auth);
    }

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)cfg->llm_timeout_ms);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, 10000L);

    CURLcode res = curl_easy_perform(curl);

    long http_code = 0;
    if (res == CURLE_OK)
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        snprintf(response, resp_cap, "curl error: %s", curl_easy_strerror(res));
        free(rb.data);
        return -1;
    }

    if (http_code != 200) {
        snprintf(response, resp_cap, "HTTP %ld: %s",
                 http_code, rb.data ? rb.data : "(no body)");
        free(rb.data);
        return -1;
    }

    /* Extract content. */
    const char *cs = find_key(rb.data, "content");
    if (cs) {
        extract_str(cs, response, resp_cap);
    } else {
        snprintf(response, resp_cap, "no content field in response");
        free(rb.data);
        return -1;
    }

    /* Extract token counts. */
    const char *pt = find_key(rb.data, "prompt_tokens");
    const char *ct = find_key(rb.data, "completion_tokens");
    *in_tok = pt ? extract_int(pt) : 0;
    *out_tok = ct ? extract_int(ct) : 0;

    free(rb.data);
    return 0;
}

/* ── Validate token string ────────────────────────────────────── */

static int validate_token_string(const char *response, char *err, size_t err_cap) {
    procgen_token_t tokens[8192];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(response, tokens, 8192, &count, err, (uint32_t)err_cap);
    return (rc == TOK_ERR_NONE) ? 0 : -1;
}

/* ── Public API ────────────────────────────────────────────────── */

int architect_run(const char *grammar_name,
                  const char *user_request,
                  const char *grammar_prompt,
                  uint32_t max_retries,
                  architect_result_t *out) {
    if (!grammar_name || !user_request || !out) return -1;

    memset(out, 0, sizeof(*out));
    llm_cost_tracker_t cost_tracker;
    llm_cost_tracker_init(&cost_tracker);

    const fr_engine_settings_t *cfg = fr_engine_settings_get();
    if (!cfg) {
        snprintf(out->error_message, sizeof(out->error_message),
                 "engine settings not initialized");
        return -1;
    }

    char prompt[65536];
    char raw_response[65536];
    char parse_err[1024];

    for (uint32_t attempt = 0; attempt <= max_retries; attempt++) {
        out->attempt_count = attempt + 1;
        parse_err[0] = '\0';

        /* Build prompt. */
        if (build_prompt(grammar_name, user_request, grammar_prompt,
                         attempt > 0 ? parse_err : NULL,
                         prompt, sizeof(prompt)) != 0) {
            snprintf(out->error_message, sizeof(out->error_message),
                     "failed to build prompt");
            return -1;
        }

        memset(raw_response, 0, sizeof(raw_response));
        int in_tok = 0, out_tok = 0;
        int rc = call_vlm(prompt, raw_response, sizeof(raw_response),
                          &in_tok, &out_tok);

        out->total_input_tokens  += (uint32_t)in_tok;
        out->total_output_tokens += (uint32_t)out_tok;

        if (rc != 0) {
            snprintf(out->error_message, sizeof(out->error_message),
                     "VLM call failed: %s", raw_response);
            continue;
        }

        /* Compute cost. */
        float cost = llm_cost_compute(in_tok, out_tok,
                                       cfg->llm_input_cost_per_1k,
                                       cfg->llm_output_cost_per_1k);
        llm_cost_tracker_add(&cost_tracker, cost);
        out->total_cost_usd = llm_cost_tracker_get(&cost_tracker);

        /* Check budget. */
        if (cfg->llm_budget_usd > 0.0f &&
            out->total_cost_usd > cfg->llm_budget_usd) {
            snprintf(out->error_message, sizeof(out->error_message),
                     "budget exceeded: $%.4f > $%.4f",
                     out->total_cost_usd, cfg->llm_budget_usd);
            return -1;
        }

        /* Validate. */
        if (validate_token_string(raw_response, parse_err, sizeof(parse_err)) == 0) {
            /* Success — strip any leading/trailing whitespace. */
            size_t rlen = strlen(raw_response);
            while (rlen > 0 && (raw_response[rlen-1] == '\n' ||
                                raw_response[rlen-1] == '\r' ||
                                raw_response[rlen-1] == ' '))
                raw_response[--rlen] = '\0';
            size_t copy = rlen < sizeof(out->token_string) - 1
                          ? rlen : sizeof(out->token_string) - 1;
            memcpy(out->token_string, raw_response, copy);
            out->token_string[copy] = '\0';
            out->success = 1;
            return 0;
        }

        /* If we have more retries, continue; otherwise fall through to failure. */
        if (attempt < max_retries) {
            fprintf(stderr, "  [architect] attempt %u failed: %s\n",
                    attempt + 1, parse_err);
        }
    }

    snprintf(out->error_message, sizeof(out->error_message),
             "failed after %u attempts: %s",
             out->attempt_count, parse_err);
    return -1;
}
