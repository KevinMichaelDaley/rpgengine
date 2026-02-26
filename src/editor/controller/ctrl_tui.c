/**
 * @file ctrl_tui.c
 * @brief TUI lifecycle — init and destroy.
 */

#include "ferrum/editor/ctrl_tui.h"
#include <stdlib.h>
#include <string.h>

bool ctrl_tui_init(ctrl_tui_t *tui) {
    if (!tui) return false;
    memset(tui, 0, sizeof(*tui));

    tui->mode      = CTRL_MODE_NORMAL;
    tui->server_fd = -1;
    tui->client_fd = -1;
    tui->cols      = 80;  /* Default until SIGWINCH. */
    tui->rows      = 24;

    /* Allocate screen buffer. */
    tui->screen_buf = (char *)malloc(CTRL_SCREEN_BUF);
    if (!tui->screen_buf) return false;
    tui->screen_cap = CTRL_SCREEN_BUF;

    /* Initialize log. */
    if (!ctrl_log_init(&tui->log, CTRL_LOG_DEFAULT_CAP)) {
        free(tui->screen_buf);
        tui->screen_buf = NULL;
        return false;
    }

    return true;
}

void ctrl_tui_destroy(ctrl_tui_t *tui) {
    if (!tui) return;
    ctrl_log_destroy(&tui->log);
    free(tui->screen_buf);
    tui->screen_buf = NULL;
}
