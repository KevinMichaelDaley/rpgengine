/**
 * @file inline_field.c
 * @brief Inline editable field widget logic.
 *
 * Non-static functions (4 / 4 limit):
 *   inline_field_begin
 *   inline_field_cancel
 *   inline_field_commit
 *   inline_field_handle_key
 */

#include "ferrum/editor/ui/inline_field.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void inline_field_begin(inline_field_ctx_t *ctx,
                         inline_field_state_t *field,
                         uint32_t field_id, float value) {
    if (!ctx || !field) return;

    /* Cancel any previously active field. */
    if (ctx->active_field && ctx->active_field != field) {
        ctx->active_field->active = false;
    }

    field->active = true;
    field->field_id = field_id;
    field->original_value = value;

    /* Format value into buffer. */
    int n = snprintf(field->buf, INLINE_FIELD_BUF_MAX, "%.3f", (double)value);
    if (n < 0) n = 0;
    if (n >= INLINE_FIELD_BUF_MAX) n = INLINE_FIELD_BUF_MAX - 1;
    field->cursor = (uint32_t)n;

    /* Strip trailing zeros for cleaner display (keep at least one decimal). */
    if (strchr(field->buf, '.')) {
        char *end = field->buf + strlen(field->buf) - 1;
        while (end > field->buf && *end == '0') {
            *end = '\0';
            end--;
        }
        /* Don't strip the digit after the decimal point. */
        if (*end == '.') {
            end[1] = '0';
            end[2] = '\0';
        }
        field->cursor = (uint32_t)strlen(field->buf);
    }

    ctx->active_field = field;
}

void inline_field_cancel(inline_field_ctx_t *ctx, float *out_value) {
    if (!ctx || !ctx->active_field) return;

    if (out_value) {
        *out_value = ctx->active_field->original_value;
    }

    ctx->active_field->active = false;
    ctx->active_field = NULL;
}

bool inline_field_commit(inline_field_ctx_t *ctx, float *out_value) {
    if (!ctx || !ctx->active_field) return false;

    char *end = NULL;
    float val = strtof(ctx->active_field->buf, &end);

    /* Reject empty or unparseable input. */
    if (end == ctx->active_field->buf || ctx->active_field->buf[0] == '\0') {
        /* Restore original on invalid input. */
        if (out_value) *out_value = ctx->active_field->original_value;
        ctx->active_field->active = false;
        ctx->active_field = NULL;
        return false;
    }

    if (out_value) *out_value = val;
    ctx->active_field->active = false;
    ctx->active_field = NULL;
    return true;
}

bool inline_field_handle_key(inline_field_ctx_t *ctx, inline_field_key_t key) {
    if (!ctx || !ctx->active_field || !ctx->active_field->active) return false;

    inline_field_state_t *f = ctx->active_field;
    uint32_t len = (uint32_t)strlen(f->buf);

    switch (key) {
    case INLINE_FIELD_KEY_BACKSPACE:
        if (f->cursor > 0) {
            memmove(f->buf + f->cursor - 1, f->buf + f->cursor,
                    len - f->cursor + 1);
            f->cursor--;
        }
        return true;

    case INLINE_FIELD_KEY_DELETE:
        if (f->cursor < len) {
            memmove(f->buf + f->cursor, f->buf + f->cursor + 1,
                    len - f->cursor);
        }
        return true;

    case INLINE_FIELD_KEY_LEFT:
        if (f->cursor > 0) f->cursor--;
        return true;

    case INLINE_FIELD_KEY_RIGHT:
        if (f->cursor < len) f->cursor++;
        return true;

    case INLINE_FIELD_KEY_HOME:
        f->cursor = 0;
        return true;

    case INLINE_FIELD_KEY_END:
        f->cursor = len;
        return true;

    case INLINE_FIELD_KEY_UP: {
        /* Increment by 0.01. */
        float val = strtof(f->buf, NULL);
        val += 0.01f;
        int n = snprintf(f->buf, INLINE_FIELD_BUF_MAX, "%.3f", (double)val);
        if (n < 0) n = 0;
        f->cursor = (uint32_t)n;
        return true;
    }

    case INLINE_FIELD_KEY_DOWN: {
        /* Decrement by 0.01. */
        float val = strtof(f->buf, NULL);
        val -= 0.01f;
        int n = snprintf(f->buf, INLINE_FIELD_BUF_MAX, "%.3f", (double)val);
        if (n < 0) n = 0;
        f->cursor = (uint32_t)n;
        return true;
    }

    default:
        return false;
    }
}
