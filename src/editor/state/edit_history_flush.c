/**
 * @file edit_history_flush.c
 * @brief Edit history — flush pending entries to JSONL log file.
 *
 * Non-static functions: 1 (edit_history_flush).
 */

#include "ferrum/editor/edit_history.h"

#include <stdio.h>
#include <string.h>

/** @brief Escape a string for JSON output (minimal: backslash and quote). */
static size_t json_escape_(const char *src, char *dst, size_t cap) {
    size_t out = 0;
    for (const char *p = src; *p && out + 2 < cap; p++) {
        if (*p == '"' || *p == '\\') {
            dst[out++] = '\\';
        }
        dst[out++] = *p;
    }
    dst[out] = '\0';
    return out;
}

uint32_t edit_history_flush(edit_history_t *history) {
    if (!history || !history->log_file || !history->entries) return 0;

    uint32_t count = 0;

    while (history->flush_cursor != history->head) {
        const edit_history_entry_t *e =
            &history->entries[history->flush_cursor];

        /* Build one JSONL line. We write the args and result as raw JSON
         * values (they're already JSON), and the ctx likewise. */
        char escaped_cmd[128];
        json_escape_(e->cmd, escaped_cmd, sizeof(escaped_cmd));

        /* Args: if non-empty, write as-is (already JSON). Else "null". */
        const char *args = (e->args[0] != '\0') ? e->args : "null";
        const char *result = (e->result[0] != '\0') ? e->result : "null";
        const char *ctx = (e->ctx[0] != '\0') ? e->ctx : "{}";

        fprintf(history->log_file,
                "{\"seq\":%llu,\"ts\":\"%s\",\"cmd\":\"%s\","
                "\"args\":%s,\"ok\":%s,\"result\":%s,\"ctx\":%s}\n",
                (unsigned long long)e->seq,
                e->timestamp,
                escaped_cmd,
                args,
                e->ok ? "true" : "false",
                result,
                ctx);

        history->flush_cursor =
            (history->flush_cursor + 1) % history->capacity;
        count++;
    }

    if (count > 0) {
        fflush(history->log_file);
    }

    return count;
}
