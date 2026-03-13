/**
 * @file scene_ui_tui.c
 * @brief TUI panel Clay UI layout: status bar, log, command input.
 *
 * Shows a status bar at the top, scrollable log area in the middle,
 * and a command input line at the bottom. All runtime text uses static
 * buffers that persist through the render pass.
 *
 * Non-static functions (3 / 4 limit):
 *   scene_ui_build_tui
 *   scene_ui_tui_log
 *   scene_ui_tui_log_error
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <stdio.h>
#include <string.h>

/* ------------------------------------------------------------------------ */
/* Static text buffers                                                       */
/* ------------------------------------------------------------------------ */

/**
 * Static buffers for TUI text. Clay_String stores only a pointer,
 * so the backing memory must survive until Clay_EndLayout() / render.
 *
 * Layout:
 *   [0] Status bar (connection + entity count + selection + sync)
 *   [1] Command input line with prompt
 */
#define TUI_STATUS_BUF_SIZE 256
static char s_status_buf[TUI_STATUS_BUF_SIZE];

/** Input line buffer with ": " prompt prepended. */
#define TUI_INPUT_BUF_SIZE (UI_TUI_INPUT_MAX + 4)
static char s_input_buf[TUI_INPUT_BUF_SIZE];

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

/**
 * @brief Internal helper to append a log line with a given type.
 */
static void tui_log_internal(scene_ui_state_t *ui, const char *text,
                              uint8_t type)
{
    if (!ui || !text) return;

    int slot = ui->tui_log_head;
    strncpy(ui->tui_log[slot], text, UI_TUI_LOG_LINE - 1);
    ui->tui_log[slot][UI_TUI_LOG_LINE - 1] = '\0';
    ui->tui_log_type[slot] = type;

    ui->tui_log_head = (ui->tui_log_head + 1) % UI_TUI_LOG_MAX;
    if (ui->tui_log_count < UI_TUI_LOG_MAX) {
        ui->tui_log_count++;
    }
}

void scene_ui_tui_log(scene_ui_state_t *ui, const char *text)
{
    tui_log_internal(ui, text, UI_TUI_LOG_NORMAL);
}

void scene_ui_tui_log_error(scene_ui_state_t *ui, const char *text)
{
    tui_log_internal(ui, text, UI_TUI_LOG_ERROR);
}

void scene_ui_build_tui(struct scene_editor *ed,
                          const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    /* Format status bar into static buffer. */
    {
        char conn_buf[64];
        char sync_buf[64];
        scene_connection_format_status(&ed->connection, conn_buf, sizeof(conn_buf));
        scene_sync_format_status(&ed->sync, sync_buf, sizeof(sync_buf));
        snprintf(s_status_buf, sizeof(s_status_buf),
                 "%s  Entities: %u  Selected: %u  %s",
                 conn_buf, ed->entities.active_count,
                 edit_selection_count(&ed->selection), sync_buf);
    }

    /* Format input line with prompt. */
    if (ed->ui.tui_active) {
        snprintf(s_input_buf, sizeof(s_input_buf), ": %s_",
                 ed->ui.tui_input);
    } else {
        snprintf(s_input_buf, sizeof(s_input_buf),
                 "Press : to enter command");
    }

    float panel_w = (float)rect->w;
    float panel_h = (float)rect->h;

    /* Root floating container positioned at the panel rectangle. */
    CLAY(CLAY_ID("TUI_Root"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(panel_w),
                       CLAY_SIZING_FIXED(panel_h)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                        THEME_PADDING_SMALL, THEME_PADDING_SMALL},
        },
        .backgroundColor = {THEME_BG_TUI_R, THEME_BG_TUI_G,
                             THEME_BG_TUI_B, THEME_BG_TUI_A},
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* ---- Status bar ---- */
        CLAY(CLAY_ID("TUI_StatusBar"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
            .backgroundColor = {THEME_ACCENT_R, THEME_ACCENT_G,
                                 THEME_ACCENT_B, 60},
        }) {
            Clay_String status_str = {
                .length = (int32_t)strlen(s_status_buf),
                .chars = s_status_buf,
            };
            Clay__OpenTextElement(status_str,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = {THEME_TEXT_R, THEME_TEXT_G,
                                  THEME_TEXT_B, THEME_TEXT_A},
                    .fontId = CLAY_FONT_MONO,
                }));
        }

        /* ---- Log area ---- */
        CLAY(CLAY_ID("TUI_Log"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .layoutDirection = CLAY_TOP_TO_BOTTOM,
                .childGap = 1,
            },
        }) {
            /* Render log lines from oldest to newest. */
            int count = ed->ui.tui_log_count;
            int start = (ed->ui.tui_log_head - count + UI_TUI_LOG_MAX)
                        % UI_TUI_LOG_MAX;
            Clay_String log_id = {.length = 7, .chars = "TUI_Log"};

            for (int i = 0; i < count; i++) {
                int idx = (start + i) % UI_TUI_LOG_MAX;
                int32_t line_len = (int32_t)strlen(ed->ui.tui_log[idx]);
                if (line_len == 0) continue;

                Clay_String line_str = {
                    .length = line_len,
                    .chars = ed->ui.tui_log[idx],
                };
                bool is_error = (ed->ui.tui_log_type[idx] == UI_TUI_LOG_ERROR);
                CLAY(CLAY_SIDI(log_id, (uint32_t)i), {
                    .layout = {
                        .sizing = {CLAY_SIZING_GROW(0),
                                   CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                        .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                                    0, 0},
                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                    },
                }) {
                    Clay__OpenTextElement(line_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = is_error
                                ? (Clay_Color){220, 60, 60, 255}
                                : (Clay_Color){THEME_TEXT_R, THEME_TEXT_G,
                                               THEME_TEXT_B, THEME_TEXT_A},
                            .fontId = CLAY_FONT_MONO,
                        }));
                }
            }
        }

        /* ---- Input line ---- */
        CLAY(CLAY_ID("TUI_Input"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT + 4)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                            2, 2},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
            .backgroundColor = ed->ui.tui_active
                ? (Clay_Color){40, 42, 48, 255}
                : (Clay_Color){30, 32, 38, 255},
        }) {
            Clay_String input_str = {
                .length = (int32_t)strlen(s_input_buf),
                .chars = s_input_buf,
            };
            Clay__OpenTextElement(input_str,
                CLAY_TEXT_CONFIG({
                    .fontSize = THEME_FONT_SIZE_UI,
                    .textColor = ed->ui.tui_active
                        ? (Clay_Color){THEME_TEXT_R, THEME_TEXT_G,
                                       THEME_TEXT_B, THEME_TEXT_A}
                        : (Clay_Color){THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                                       THEME_TEXT_DIM_B, THEME_TEXT_DIM_A},
                    .fontId = CLAY_FONT_MONO,
                }));
        }
    }
}
