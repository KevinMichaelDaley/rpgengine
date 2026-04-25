/**
 * @file llm_smoke_test.c
 * @brief Smoke test: call Ollama qwen2.5-coder:1.5b via OpenAI-compatible API.
 *
 * Run after starting Ollama:
 *   ollama run qwen2.5-coder:1.5b
 * Then:
 *   ./build/llm_smoke_test
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>

#include "ferrum/engine_settings.h"
#include "ferrum/llm/llm_cost_tracker.h"

/* ── Response buffer ───────────────────────────────────────────── */

typedef struct {
    char *data;
    size_t size;
} response_buf_t;

static size_t write_cb(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    response_buf_t *rb = (response_buf_t *)userp;
    char *ptr = realloc(rb->data, rb->size + realsize + 1);
    if (!ptr) return 0;
    rb->data = ptr;
    memcpy(rb->data + rb->size, contents, realsize);
    rb->size += realsize;
    rb->data[rb->size] = '\0';
    return realsize;
}

/* ── Minimal JSON extraction ───────────────────────────────────── */

static const char *find_key(const char *json, const char *key) {
    size_t klen = strlen(key);
    const char *p = json;
    while ((p = strstr(p, key)) != NULL) {
        /* Verify it looks like a JSON key: preceded by quote, followed by quote colon. */
        if (p > json && p[-1] == '"' && p[klen] == '"' && p[klen + 1] == ':') {
            /* Find the start of the value string. */
            const char *v = p + klen + 2;
            while (*v == ' ' || *v == '\t' || *v == '\n' || *v == '\r') v++;
            if (*v == '"') return v + 1; /* start of string value */
            return v; /* non-string value */
        }
        p += klen;
    }
    return NULL;
}

static void extract_string(const char *start, char *out, size_t out_cap) {
    size_t i = 0;
    while (start[i] != '"' && start[i] != '\0' && i < out_cap - 1) {
        out[i] = start[i];
        i++;
    }
    out[i] = '\0';
}

static int extract_int(const char *start) {
    return atoi(start);
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    printf("=== LLM Smoke Test (Ollama qwen2.5-coder:1.5b) ===\n\n");

    /* 1. Configure engine settings. */
    fr_engine_settings_init();
    fr_engine_settings_t *s = fr_engine_settings_mut();
    if (!s) {
        printf("FAIL: could not get mutable settings\n");
        return 1;
    }
    strncpy(s->llm_base_url, "http://localhost:11434/v1", sizeof(s->llm_base_url) - 1);
    strncpy(s->llm_model, "qwen2.5-coder:1.5b", sizeof(s->llm_model) - 1);
    s->llm_timeout_ms = 30000;
    s->llm_max_tokens = 256;
    s->llm_input_cost_per_1k = 0.0f;  /* local model — no real cost */
    s->llm_output_cost_per_1k = 0.0f;
    s->llm_budget_usd = 0.0f;          /* unlimited */
    fr_engine_settings_freeze();

    const fr_engine_settings_t *cfg = fr_engine_settings_get();
    printf("Provider : %s\n", cfg->llm_base_url);
    printf("Model    : %s\n", cfg->llm_model);
    printf("Timeout  : %u ms\n", cfg->llm_timeout_ms);
    printf("Max tok  : %u\n\n", cfg->llm_max_tokens);

    /* 2. Build JSON payload. */
    const char *prompt = "Say exactly 'pong' and nothing else.";
    char payload[1024];
    int plen = snprintf(payload, sizeof(payload),
        "{"
        "\"model\":\"%s\","
        "\"messages\":[{\"role\":\"user\",\"content\":\"%s\"}],"
        "\"max_tokens\":%u,"
        "\"temperature\":0.0"
        "}",
        cfg->llm_model, prompt, cfg->llm_max_tokens);
    if (plen < 0 || (size_t)plen >= sizeof(payload)) {
        printf("FAIL: payload too large\n");
        return 1;
    }

    /* 3. HTTP POST via libcurl. */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    CURL *curl = curl_easy_init();
    if (!curl) {
        printf("FAIL: curl_easy_init failed\n");
        return 1;
    }

    char url[320];
    snprintf(url, sizeof(url), "%s/chat/completions", cfg->llm_base_url);

    response_buf_t rb = {0};
    struct curl_slist *headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_cb);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &rb);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, (long)cfg->llm_timeout_ms);

    printf("POST %s\n", url);
    printf("Payload: %s\n\n", payload);

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        printf("FAIL: curl error: %s\n", curl_easy_strerror(res));
        curl_easy_cleanup(curl);
        curl_slist_free_all(headers);
        free(rb.data);
        return 1;
    }

    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    printf("HTTP %ld\n", http_code);
    printf("Raw response:\n%s\n\n", rb.data ? rb.data : "(empty)");

    if (http_code != 200) {
        printf("FAIL: unexpected HTTP status\n");
        free(rb.data);
        return 1;
    }

    /* 4. Parse minimal JSON. */
    const char *content_start = find_key(rb.data, "content");
    const char *prompt_tokens   = find_key(rb.data, "prompt_tokens");
    const char *completion_tokens = find_key(rb.data, "completion_tokens");

    if (!content_start) {
        printf("FAIL: no 'content' in response\n");
        free(rb.data);
        return 1;
    }

    char content[512];
    extract_string(content_start, content, sizeof(content));

    int in_tok = prompt_tokens ? extract_int(prompt_tokens) : 0;
    int out_tok = completion_tokens ? extract_int(completion_tokens) : 0;

    /* 5. Cost tracking smoke. */
    llm_cost_tracker_t ct;
    llm_cost_tracker_init(&ct);
    float cost = llm_cost_compute(in_tok, out_tok,
                                   cfg->llm_input_cost_per_1k,
                                   cfg->llm_output_cost_per_1k);
    llm_cost_tracker_add(&ct, cost);

    printf("Content : %s\n", content);
    printf("In tok  : %d\n", in_tok);
    printf("Out tok : %d\n", out_tok);
    printf("Cost    : $%.6f\n", cost);
    printf("Total   : $%.6f\n\n", llm_cost_tracker_get(&ct));

    /* 6. Validate response contains expected token. */
    if (strstr(content, "pong") == NULL) {
        printf("FAIL: expected 'pong' in response\n");
        free(rb.data);
        return 1;
    }

    printf("PASS: smoke test complete.\n");
    free(rb.data);
    curl_global_cleanup();
    return 0;
}
