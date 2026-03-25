/**
 * @file inline_field_text.c
 * @brief Inline field text character handling.
 *
 * Non-static functions (1 / 4 limit):
 *   inline_field_handle_text
 */

#include "ferrum/editor/ui/inline_field.h"
#include <string.h>

bool inline_field_handle_text(inline_field_ctx_t *ctx, char ch) {
    if (!ctx || !ctx->active_field || !ctx->active_field->active) return false;

    /* Only accept numeric characters. */
    bool valid = (ch >= '0' && ch <= '9') || ch == '.' || ch == '-';
    if (!valid) return false;

    inline_field_state_t *f = ctx->active_field;
    uint32_t len = (uint32_t)strlen(f->buf);

    /* Reject if buffer full. */
    if (len >= INLINE_FIELD_BUF_MAX - 1) return false;

    /* Only allow one decimal point. */
    if (ch == '.' && strchr(f->buf, '.')) return false;

    /* Only allow minus at the start. */
    if (ch == '-' && f->cursor != 0) return false;
    if (ch == '-' && strchr(f->buf, '-')) return false;

    /* Insert character at cursor position. */
    memmove(f->buf + f->cursor + 1, f->buf + f->cursor,
            len - f->cursor + 1);
    f->buf[f->cursor] = ch;
    f->cursor++;

    return true;
}
