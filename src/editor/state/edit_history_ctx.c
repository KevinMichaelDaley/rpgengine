/**
 * @file edit_history_ctx.c
 * @brief Edit history — context snapshot builder.
 *
 * Captures cursor position, selected entity IDs, and active @ alias
 * entities into a JSON string for history entries.
 *
 * Non-static functions: 1 (edit_history_snapshot_ctx).
 */

#include "ferrum/editor/edit_history.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"

#include <stdio.h>
#include <string.h>

/** @brief Maximum number of selected IDs to include inline. */
#define MAX_INLINE_SEL 16

/** @brief Maximum number of @ aliases to include. */
#define MAX_ALIASES    32

size_t edit_history_snapshot_ctx(const struct edit_cmd_ctx *ctx,
                                char *buf, size_t cap) {
    if (!ctx || !buf || cap == 0) return 0;

    size_t off = 0;

    #define APPEND(...) do { \
        int _n = snprintf(buf + off, cap > off ? cap - off : 0, \
                          __VA_ARGS__); \
        if (_n > 0) off += (size_t)_n; \
    } while (0)

    APPEND("{");

    /* ── Cursor ────────────────────────────────────────────────── */
    if (ctx->cursor_stack_count > 0) {
        uint32_t top = ctx->cursor_stack_count - 1;
        APPEND("\"cursor\":[%.6g,%.6g,%.6g]",
               (double)ctx->cursor_stack[top][0],
               (double)ctx->cursor_stack[top][1],
               (double)ctx->cursor_stack[top][2]);
    } else {
        APPEND("\"cursor\":[0,0,0]");
    }

    /* ── Selection ─────────────────────────────────────────────── */
    if (ctx->selection) {
        uint32_t sel_count = edit_selection_count(ctx->selection);
        const uint32_t *ids = edit_selection_ids(ctx->selection);

        if (sel_count <= MAX_INLINE_SEL) {
            APPEND(",\"sel\":[");
            for (uint32_t i = 0; i < sel_count; i++) {
                if (i > 0) APPEND(",");
                APPEND("%u", ids[i]);
            }
            APPEND("]");
        } else {
            /* Too many to list — just store the count. */
            APPEND(",\"sel_count\":%u", sel_count);
        }
    } else {
        APPEND(",\"sel\":[]");
    }

    /* ── Aliases (@ entities) ──────────────────────────────────── */
    if (ctx->entities) {
        APPEND(",\"aliases\":{");
        uint32_t alias_count = 0;
        for (uint32_t i = 0; i < ctx->entities->capacity &&
             alias_count < MAX_ALIASES; i++) {
            const edit_entity_t *e = &ctx->entities->entities[i];
            if (!e->active || e->name[0] != '@') continue;

            if (alias_count > 0) APPEND(",");
            APPEND("\"%s\":{\"pos\":[%.6g,%.6g,%.6g],"
                   "\"rot\":[%.6g,%.6g,%.6g]}",
                   e->name,
                   (double)e->pos[0], (double)e->pos[1],
                   (double)e->pos[2],
                   (double)e->rot[0], (double)e->rot[1],
                   (double)e->rot[2]);
            alias_count++;
        }
        APPEND("}");
    }

    APPEND("}");
    #undef APPEND

    if (off < cap) buf[off] = '\0';
    return off;
}
