/**
 * @file edit_history.c
 * @brief Edit history — ring buffer lifecycle and recording.
 *
 * Non-static functions: 3 (edit_history_init, edit_history_record,
 *   edit_history_destroy).
 */

#include "ferrum/editor/edit_history.h"
#include "ferrum/editor/edit_cmd_ctx.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Helpers ─────────────────────────────────────────────────────── */

/** @brief Write current UTC time as ISO-8601 into buf. */
static void timestamp_now_(char *buf, size_t cap) {
    time_t now = time(NULL);
    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(buf, cap, "%Y-%m-%dT%H:%M:%SZ", &utc);
}

/** @brief Copy a NUL-terminated string into a fixed buffer, truncating. */
static void copy_str_(char *dst, size_t cap, const char *src) {
    if (!src) {
        dst[0] = '\0';
        return;
    }
    size_t len = strlen(src);
    if (len >= cap) len = cap - 1;
    memcpy(dst, src, len);
    dst[len] = '\0';
}

/* ── Lifecycle ───────────────────────────────────────────────────── */

bool edit_history_init(edit_history_t *history, uint32_t capacity,
                       const char *log_path) {
    if (!history) return false;
    memset(history, 0, sizeof(*history));

    if (capacity == 0) capacity = EDIT_HISTORY_RING_CAP;

    history->entries = calloc(capacity, sizeof(edit_history_entry_t));
    if (!history->entries) return false;
    history->capacity = capacity;

    if (log_path) {
        history->log_file = fopen(log_path, "a");
        if (!history->log_file) {
            free(history->entries);
            history->entries = NULL;
            return false;
        }
    }

    return true;
}

void edit_history_destroy(edit_history_t *history) {
    if (!history) return;

    /* Flush remaining entries before closing. */
    if (history->log_file) {
        edit_history_flush(history);
        fclose(history->log_file);
        history->log_file = NULL;
    }

    free(history->entries);
    history->entries = NULL;
    history->capacity = 0;
}

/* ── Recording ───────────────────────────────────────────────────── */

void edit_history_record(edit_history_t *history,
                         const struct edit_cmd_ctx *ctx,
                         const char *cmd,
                         const char *args_json,
                         const char *result_json,
                         bool ok) {
    if (!history || !history->entries || !cmd) return;

    edit_history_entry_t *e = &history->entries[history->head];

    /* Sequence and timestamp. */
    history->seq++;
    e->seq = history->seq;
    timestamp_now_(e->timestamp, sizeof(e->timestamp));

    /* Command data. */
    copy_str_(e->cmd, sizeof(e->cmd), cmd);
    copy_str_(e->args, sizeof(e->args), args_json);
    copy_str_(e->result, sizeof(e->result), result_json);
    e->ok = ok;

    /* Context snapshot. */
    if (ctx) {
        edit_history_snapshot_ctx(ctx, e->ctx, sizeof(e->ctx));
    } else {
        e->ctx[0] = '\0';
    }

    /* Advance ring head. */
    history->head = (history->head + 1) % history->capacity;
}
