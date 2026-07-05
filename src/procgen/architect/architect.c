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
    while (s && *s && i < cap - 1) {
        if (*s == '\\' && *(s + 1)) {
            s++;
            switch (*s) {
            case 'n':  out[i++] = '\n'; break;
            case 'r':  out[i++] = '\r'; break;
            case 't':  out[i++] = '\t'; break;
            case '"':  out[i++] = '"';  break;
            case '\\': out[i++] = '\\'; break;
            default:   out[i++] = '\\'; out[i++] = *s; break;
            }
            s++;
        } else if (*s == '"') {
            break;  /* closing quote */
        } else {
            out[i++] = *s++;
        }
    }
    out[i] = '\0';
}

static int extract_int(const char *s) { return atoi(s); }

/* ── Default system prompt ────────────────────────────────────── */

static const char *DEFAULT_SYSTEM_PROMPT =
    "You are a dungeon architect for a survival/crafting RPG. "
    "You generate level layouts as multi-floor ASCII floor plans with "
    "loss function specifications.\n\n"

    "FORMAT:\n"
    "=== FLOOR <N>: <label> ===\n"
    "<grid of space-separated characters>\n"
    "=== FLOOR <N+1>: <label> ===\n"
    "<grid of space-separated characters>\n"
    "LOSS:\n"
    "  <loss_term_1>\n"
    "  <loss_term_2>\n\n"

    "CHARACTER LEGEND:\n"
    "  W = Outer wall / boundary (not a room)\n"
    "  B = Bar area        R = Common room\n"
    "  P = Private bedroom  G = Entrance\n"
    "  ^ = Stairs up        v = Stairs down\n"
    "  . = Open floor (merged into adjacent rooms)\n\n"

    "GRID RULES:\n"
    "1. Outer border must be entirely W.\n"
    "2. Each contiguous region of same character forms one room.\n"
    "3. Different character types sharing an edge = adjacency/connection.\n"
    "4. ^ and v stair markers must be adjacent to room cells.\n"
    "5. Floor 0 ^ must correspond to Floor 1 v in matching positions.\n"
    "6. Grids must be rectangular with consistent row widths.\n"
    "7. Minimum 3x3 interior space (not counting wall border).\n\n"

    "LOSS FORMAT:\n"
    "After the last floor block, include a LOSS: section with one term per line:\n"
    "  PathDistance(room_a, room_b) [>|<|=] [distance]\n"
    "  LineOfSight(room_a, room_b)\n"
    "  NonPenetration(all)\n"
    "  MinimumSize(rooms..., min_m)\n"
    "  Separation(type_A, type_B) [>|<|=] [distance]\n"
    "  Containment(room, region)\n"
    "  AdjacencyCount(room, n)\n"
    "  HeightSpan(room, min_y, max_y)\n"
    "  StairAlignment(anchor_from, anchor_to)\n"
    "  FloorAccessibility(floor)\n\n"

    "EXAMPLE:\n"
    "=== FLOOR 0: GROUND ===\n"
    "W W W W W W\n"
    "W B B R R W\n"
    "W B B R R W\n"
    "W W W G W W\n"
    "=== FLOOR 1: UPPER ===\n"
    "W W W W W W\n"
    "W P P P P W\n"
    "W P P P P W\n"
    "W W W W W W\n"
    "LOSS:\n"
    "  PathDistance(entrance, bar) < 20\n"
    "  NonPenetration(all)\n"
    "  MinimumSize(all_rooms, 6)\n\n"

    "RULES:\n"
    "1. Output ONLY the ASCII floor plan + LOSS block. No explanation.\n"
    "2. All grids must be surrounded by W on all four edges.\n"
    "3. Every room type in the prompt must appear in the grid.\n"
    "4. Stair markers (^, v) must have matching counterparts on adjacent floors.\n\n"

    "USER REQUEST: ";

/* ── Build system prompt ──────────────────────────────────────── */

int procgen_architect_build_prompt(const char *grammar_name,
                        const char *user_request,
                        const char *grammar_prompt,
                        const char *error_context,
                        char *out, size_t out_cap) {
    const char *sys = grammar_prompt ? grammar_prompt : DEFAULT_SYSTEM_PROMPT;
    (void)grammar_name;

    if (error_context && error_context[0]) {
        return snprintf(out, out_cap,
            "%s\n\n"
            "USER REQUEST: %s\n\n"
            "PREVIOUS ATTEMPT FAILED: %s\n"
            "Please correct the errors and output ONLY the ASCII floor plan.",
            sys, user_request, error_context);
    } else {
        return snprintf(out, out_cap,
            "%s\n\n"
            "USER REQUEST: %s\n\n"
            "Output ONLY the ASCII floor plan + LOSS block. No explanation.",
            sys, user_request);
    }
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

static int validate_ascii_grid(const char *response, char *err, size_t err_cap) {
    if (!response || !err || err_cap == 0) return -1;

    /* Check for at least one floor header */
    if (!strstr(response, "=== FLOOR") && !strstr(response, "=== Floor")) {
        snprintf(err, err_cap, "Missing === FLOOR header");
        return -1;
    }

    /* Check for LOSS: section */
    if (!strstr(response, "LOSS:") && !strstr(response, "Loss:")) {
        snprintf(err, err_cap, "Missing LOSS: section");
        return -1;
    }

    return 0;
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
        if (procgen_architect_build_prompt(grammar_name, user_request, grammar_prompt,
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
        if (validate_ascii_grid(raw_response, parse_err, sizeof(parse_err)) == 0) {
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
