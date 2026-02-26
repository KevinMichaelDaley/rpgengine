/**
 * @file ctrl_render.c
 * @brief TUI rendering — builds ANSI escape sequence buffer.
 *
 * Double-buffered: builds full screen content in screen_buf, then
 * the caller writes it to stdout in a single write() call.
 *
 * Layout:
 *   Row 1:          Status bar (inverse video)
 *   Rows 2..N-1:    Log area (scrollable)
 *   Row N:          Command line
 */

#include "ferrum/editor/ctrl_tui.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

/** @brief Append to screen buffer. */
static void emit_(ctrl_tui_t *tui, const char *data, size_t len) {
    if (tui->screen_len + len > tui->screen_cap) return;
    memcpy(tui->screen_buf + tui->screen_len, data, len);
    tui->screen_len += len;
}

/** @brief Append formatted text to screen buffer. */
static void emitf_(ctrl_tui_t *tui, const char *fmt, ...) {
    char tmp[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(tmp, sizeof(tmp), fmt, ap);
    va_end(ap);
    if (n > 0) emit_(tui, tmp, (size_t)n);
}

/** @brief Render the status bar on row 1. */
static void render_status_bar_(ctrl_tui_t *tui) {
    /* Move to row 1, col 1. Inverse video. */
    emitf_(tui, "\033[1;1H\033[7m");

    /* Mode indicator. */
    const char *mode_str = "NORMAL";
    switch (tui->mode) {
        case CTRL_MODE_COMMAND: mode_str = "COMMAND"; break;
        case CTRL_MODE_REPL:    mode_str = "REPL";    break;
        case CTRL_MODE_GRAB:    mode_str = "GRAB";    break;
        case CTRL_MODE_CONTEXT: mode_str = "CONTEXT"; break;
        default: break;
    }

    char line[256];
    int len = snprintf(line, sizeof(line), " [%s] ", mode_str);

    /* Pad to terminal width. */
    int pad = tui->cols - len;
    if (pad < 0) pad = 0;

    emit_(tui, line, (size_t)len);
    for (int i = 0; i < pad; i++) emit_(tui, " ", 1);

    /* Reset attributes. */
    emit_(tui, "\033[0m", 4);
}

/** @brief ANSI color code for log level. */
static const char *level_color_(uint8_t level) {
    switch (level) {
        case 1:  return "\033[33m"; /* Yellow for warnings. */
        case 2:  return "\033[31m"; /* Red for errors. */
        default: return "\033[0m";  /* Default for info. */
    }
}

/** @brief Render the log area (rows 2 to rows-1). */
static void render_log_area_(ctrl_tui_t *tui) {
    int log_rows = tui->rows - 2; /* Minus status bar and command line. */
    if (log_rows < 1) return;

    for (int row = 0; row < log_rows; row++) {
        /* Visual row: top of log area = oldest visible.
         * index from bottom: (log_rows - 1 - row). */
        uint32_t idx = (uint32_t)(log_rows - 1 - row);
        const ctrl_log_entry_t *e = ctrl_log_get(&tui->log, idx);

        /* Move to screen row (row + 2, since row 1 is status bar). */
        emitf_(tui, "\033[%d;1H\033[K", row + 2);

        if (e) {
            emit_(tui, level_color_(e->level), strlen(level_color_(e->level)));
            /* Truncate to terminal width. */
            size_t text_len = strlen(e->text);
            if (text_len > (size_t)tui->cols) text_len = (size_t)tui->cols;
            emit_(tui, e->text, text_len);
            emit_(tui, "\033[0m", 4);
        }
    }
}

/** @brief Render the command line on the last row. */
static void render_command_line_(ctrl_tui_t *tui) {
    emitf_(tui, "\033[%d;1H\033[K", tui->rows);

    if (tui->mode == CTRL_MODE_COMMAND) {
        /* Show ':' prefix + command text. */
        emit_(tui, "\033[36m:\033[0m", 13); /* Cyan colon. */
        if (tui->cmd_len > 0) {
            emit_(tui, tui->cmd_text, tui->cmd_len);
        }
    } else {
        /* Show hint in Normal mode. */
        const char *hint = "Press ':' to enter a command";
        emit_(tui, "\033[90m", 5); /* Dim gray. */
        emit_(tui, hint, strlen(hint));
        emit_(tui, "\033[0m", 4);
    }
}

void ctrl_tui_render(ctrl_tui_t *tui) {
    if (!tui || !tui->screen_buf) return;
    tui->screen_len = 0;

    /* Hide cursor during render. */
    emit_(tui, "\033[?25l", 6);

    render_status_bar_(tui);
    render_log_area_(tui);
    render_command_line_(tui);

    /* Show cursor and position it. */
    if (tui->mode == CTRL_MODE_COMMAND) {
        /* Position cursor after ':' + text. */
        emitf_(tui, "\033[%d;%dH", tui->rows, 2 + (int)tui->cmd_len);
    }
    emit_(tui, "\033[?25h", 6); /* Show cursor. */
}
