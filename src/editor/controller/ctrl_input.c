/**
 * @file ctrl_input.c
 * @brief TUI input state machine — keystroke processing and mode transitions.
 */

#include "ferrum/editor/ctrl_tui.h"
#include <string.h>

/** @brief Static buffer for returning command strings from feed_key. */
static char s_cmd_result[CTRL_CMD_MAX_LEN];

/**
 * @brief Handle a key in Normal mode.
 *
 * Digits accumulate a numeric prefix. ':' enters Command mode.
 * Other keys are hotkey dispatches (not yet wired to commands).
 */
static const char *handle_normal_(ctrl_tui_t *tui, char ch) {
    /* Digit → numeric prefix accumulation. */
    if (ch >= '0' && ch <= '9' && !(ch == '0' && tui->numeric_prefix == 0)) {
        tui->numeric_prefix = tui->numeric_prefix * 10 + (uint32_t)(ch - '0');
        return NULL;
    }

    /* ':' → enter Command mode. */
    if (ch == ':') {
        tui->mode = CTRL_MODE_COMMAND;
        tui->cmd_len = 0;
        tui->cmd_cursor = 0;
        tui->cmd_text[0] = '\0';
        tui->numeric_prefix = 0;
        return NULL;
    }

    /* Other keys: reset prefix, no command produced. */
    tui->numeric_prefix = 0;
    return NULL;
}

/**
 * @brief Handle a key in Command mode.
 *
 * Printable chars add to command buffer. Enter submits.
 * Escape cancels. Backspace deletes.
 */
static const char *handle_command_(ctrl_tui_t *tui, char ch) {
    /* ESC → cancel and return to Normal. */
    if (ch == 0x1B) {
        tui->mode = CTRL_MODE_NORMAL;
        tui->cmd_len = 0;
        tui->cmd_text[0] = '\0';
        return NULL;
    }

    /* Enter → submit command. */
    if (ch == '\r' || ch == '\n') {
        if (tui->cmd_len == 0) {
            tui->mode = CTRL_MODE_NORMAL;
            return NULL;
        }
        /* Copy command to static return buffer. */
        memcpy(s_cmd_result, tui->cmd_text, tui->cmd_len);
        s_cmd_result[tui->cmd_len] = '\0';

        tui->mode = CTRL_MODE_NORMAL;
        tui->cmd_len = 0;
        tui->cmd_text[0] = '\0';
        return s_cmd_result;
    }

    /* Backspace/DEL → delete last char or exit if empty. */
    if (ch == 0x7F || ch == 0x08) {
        if (tui->cmd_len == 0) {
            tui->mode = CTRL_MODE_NORMAL;
            return NULL;
        }
        tui->cmd_len--;
        tui->cmd_text[tui->cmd_len] = '\0';
        if (tui->cmd_cursor > tui->cmd_len) {
            tui->cmd_cursor = tui->cmd_len;
        }
        return NULL;
    }

    /* Printable character → insert at end. */
    if (ch >= 0x20 && ch < 0x7F && tui->cmd_len < CTRL_CMD_MAX_LEN - 1) {
        tui->cmd_text[tui->cmd_len++] = ch;
        tui->cmd_text[tui->cmd_len] = '\0';
        tui->cmd_cursor = tui->cmd_len;
    }

    return NULL;
}

const char *ctrl_tui_feed_key(ctrl_tui_t *tui, char ch) {
    if (!tui) return NULL;

    switch (tui->mode) {
        case CTRL_MODE_NORMAL:  return handle_normal_(tui, ch);
        case CTRL_MODE_COMMAND: return handle_command_(tui, ch);
        case CTRL_MODE_REPL:
        case CTRL_MODE_GRAB:
        case CTRL_MODE_CONTEXT:
            /* ESC returns to Normal from any mode. */
            if (ch == 0x1B) {
                tui->mode = CTRL_MODE_NORMAL;
            }
            return NULL;
    }
    return NULL;
}
