/**
 * @file tui_panel_log.c
 * @brief TUI panel log operations and status formatting.
 */

#include "ferrum/editor/ui/tui_panel.h"

#include <stdio.h>
#include <string.h>

void tui_panel_log(tui_panel_t *panel, uint8_t level, const char *text) {
    if (!panel || !panel->initialized) return;
    ctrl_log_add(&panel->tui.log, level, text);
}

void tui_panel_log_cmd(tui_panel_t *panel, const char *text, uint32_t cmd_id) {
    if (!panel || !panel->initialized) return;
    ctrl_log_add_cmd(&panel->tui.log, text, cmd_id);
}

void tui_panel_format_status(const tui_panel_t *panel, char *buf, size_t cap) {
    if (!panel || !buf || cap == 0) return;

    const char *mode_str = "NORMAL";
    switch (panel->tui.mode) {
        case CTRL_MODE_NORMAL:  mode_str = "NORMAL";  break;
        case CTRL_MODE_COMMAND: mode_str = "COMMAND"; break;
        case CTRL_MODE_REPL:    mode_str = "REPL";    break;
        case CTRL_MODE_GRAB:    mode_str = "GRAB";    break;
        case CTRL_MODE_CONTEXT: mode_str = "CONTEXT"; break;
    }

    uint32_t log_count = ctrl_log_visible_count(&panel->tui.log);
    snprintf(buf, cap, "Mode: %s | Log: %u entries", mode_str, log_count);
}
