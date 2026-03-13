/**
 * @file scene_ui_tui_resolve.c
 * @brief TUI log status backfill: resolve pending entries by cmd_id.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_tui_log_resolve
 */

#include "ferrum/editor/scene/scene_ui.h"

void scene_ui_tui_log_resolve(scene_ui_state_t *ui, uint32_t cmd_id, bool ok)
{
    if (!ui || cmd_id == 0) return;

    /* Search the ring buffer for a PENDING entry with this cmd_id.
     * Walk backwards from newest to oldest for fast match. */
    for (int i = 0; i < ui->tui_log_count; i++) {
        int idx = (ui->tui_log_head - 1 - i + UI_TUI_LOG_MAX) % UI_TUI_LOG_MAX;
        if (ui->tui_log_type[idx] == UI_TUI_LOG_PENDING &&
            ui->tui_log_cmd_id[idx] == cmd_id) {
            ui->tui_log_type[idx] = ok ? UI_TUI_LOG_SUCCESS : UI_TUI_LOG_ERROR;
            return;
        }
    }
}
