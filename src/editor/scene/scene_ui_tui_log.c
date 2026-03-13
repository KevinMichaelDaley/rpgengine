/**
 * @file scene_ui_tui_log.c
 * @brief TUI log ring buffer: append, resolve, status backfill.
 *
 * Non-static functions (4 / 4 limit):
 *   scene_ui_tui_log
 *   scene_ui_tui_log_error
 *   scene_ui_tui_log_success
 *   scene_ui_tui_log_pending
 *
 * scene_ui_tui_log_resolve is in scene_ui_tui_resolve.c.
 */

#include "ferrum/editor/scene/scene_ui.h"

#include <SDL2/SDL.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Internal helper                                                           */
/* ------------------------------------------------------------------------ */

/**
 * @brief Append a log line with a given type and optional cmd_id.
 */
static void tui_log_internal(scene_ui_state_t *ui, const char *text,
                              uint8_t type, uint32_t cmd_id)
{
    if (!ui || !text) return;

    int slot = ui->tui_log_head;
    strncpy(ui->tui_log[slot], text, UI_TUI_LOG_LINE - 1);
    ui->tui_log[slot][UI_TUI_LOG_LINE - 1] = '\0';
    ui->tui_log_type[slot] = type;
    ui->tui_log_cmd_id[slot] = cmd_id;
    ui->tui_log_timestamp[slot] = SDL_GetTicks();

    ui->tui_log_head = (ui->tui_log_head + 1) % UI_TUI_LOG_MAX;
    if (ui->tui_log_count < UI_TUI_LOG_MAX) {
        ui->tui_log_count++;
    }

    /* Auto-scroll to bottom on new content. */
    ui->tui_log_scroll = 0;
}

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void scene_ui_tui_log(scene_ui_state_t *ui, const char *text)
{
    tui_log_internal(ui, text, UI_TUI_LOG_NORMAL, 0);
}

void scene_ui_tui_log_error(scene_ui_state_t *ui, const char *text)
{
    tui_log_internal(ui, text, UI_TUI_LOG_ERROR, 0);
}

void scene_ui_tui_log_success(scene_ui_state_t *ui, const char *text)
{
    tui_log_internal(ui, text, UI_TUI_LOG_SUCCESS, 0);
}

void scene_ui_tui_log_pending(scene_ui_state_t *ui, const char *text,
                               uint32_t cmd_id)
{
    tui_log_internal(ui, text, UI_TUI_LOG_PENDING, cmd_id);
}
