/**
 * @file tui_panel.c
 * @brief TUI panel lifecycle and input — init, destroy, feed_key, scroll.
 */

#include "ferrum/editor/ui/tui_panel.h"

#include <string.h>

bool tui_panel_init(tui_panel_t *panel, const tui_panel_config_t *config) {
    if (!panel) return false;
    memset(panel, 0, sizeof(*panel));

    uint32_t cap = (config && config->log_capacity > 0)
                       ? config->log_capacity
                       : 0; /* 0 = ctrl_log default */

    if (!ctrl_log_init(&panel->tui.log, cap)) return false;

    panel->tui.mode           = CTRL_MODE_NORMAL;
    panel->tui.numeric_prefix = 0;
    panel->tui.cmd_len        = 0;
    panel->tui.cmd_cursor     = 0;
    panel->tui.cmd_text[0]    = '\0';
    panel->tui.server_fd      = -1;
    panel->tui.client_fd      = -1;
    panel->tui.raw_mode       = false;
    panel->initialized        = true;
    return true;
}

void tui_panel_destroy(tui_panel_t *panel) {
    if (!panel || !panel->initialized) return;
    ctrl_log_destroy(&panel->tui.log);
    panel->initialized = false;
}

const char *tui_panel_feed_key(tui_panel_t *panel, char ch) {
    if (!panel || !panel->initialized) return NULL;
    return ctrl_tui_feed_key(&panel->tui, ch);
}

void tui_panel_scroll(tui_panel_t *panel, int delta) {
    if (!panel || !panel->initialized) return;
    if (delta > 0) {
        ctrl_log_scroll_up(&panel->tui.log, (uint32_t)delta);
    } else if (delta < 0) {
        ctrl_log_scroll_down(&panel->tui.log, (uint32_t)(-delta));
    }
}
