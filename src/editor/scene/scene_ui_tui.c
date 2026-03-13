/**
 * @file scene_ui_tui.c
 * @brief TUI panel Clay UI layout: status bar, log, command input.
 *
 * Shows a status bar at the top, scrollable log area in the middle,
 * and a command input line at the bottom. All runtime text uses static
 * buffers that persist through the render pass.
 *
 * Non-static functions (1 / 4 limit):
 *   scene_ui_build_tui
 */

#include "ferrum/editor/scene/scene_ui.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_panel.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/ui/clay_theme.h"
#include "ferrum/editor/ui/clay_fonts.h"
#include "clay.h"

#include <SDL2/SDL.h>
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

/** Buffer for text before cursor (": " + chars before cursor). */
static char s_input_pre[TUI_INPUT_BUF_SIZE];

/** Buffer for char at cursor position (single char or space). */
static char s_input_cur[4];

/** Buffer for text after cursor. */
static char s_input_post[TUI_INPUT_BUF_SIZE];

/* ------------------------------------------------------------------------ */
/* Public API                                                                */
/* ------------------------------------------------------------------------ */

void scene_ui_build_tui(struct scene_editor *ed,
                          const struct panel_rect *rect) {
    if (!ed || !rect || rect->w <= 0 || rect->h <= 0) {
        return;
    }

    uint32_t now_ms = SDL_GetTicks();

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

    /* Format input line segments for cursor rendering. */
    if (ed->ui.tui_active) {
        int cur = ed->ui.tui_cursor;
        int len = ed->ui.tui_input_len;
        /* Pre-cursor: prompt + text before cursor. */
        snprintf(s_input_pre, sizeof(s_input_pre), ": %.*s",
                 cur, ed->ui.tui_input);
        /* Char at cursor (space if at end). */
        if (cur < len) {
            s_input_cur[0] = ed->ui.tui_input[cur];
            s_input_cur[1] = '\0';
        } else {
            s_input_cur[0] = ' ';
            s_input_cur[1] = '\0';
        }
        /* Post-cursor: remaining text after cursor char. */
        if (cur + 1 < len) {
            snprintf(s_input_post, sizeof(s_input_post), "%s",
                     ed->ui.tui_input + cur + 1);
        } else {
            s_input_post[0] = '\0';
        }
    }
    /* Fallback single-string for inactive mode. */
    if (!ed->ui.tui_active) {
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
        .clip = {.horizontal = true, .vertical = true},
        .floating = {
            .attachTo = CLAY_ATTACH_TO_ROOT,
            .offset = {(float)rect->x, (float)rect->y},
        },
    }) {
        /* ---- Status bar (doubles as title bar / focus indicator) ---- */
        bool tui_focused = (ed->layout.focus == PANEL_TUI);
        uint8_t tui_title_alpha = tui_focused ? 140 : 60;
        CLAY(CLAY_ID("TUI_StatusBar"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL, 0, 0},
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
            .backgroundColor = {THEME_ACCENT_R, THEME_ACCENT_G,
                                 THEME_ACCENT_B, tui_title_alpha},
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

        /* ---- Log area (manual windowing + scrollbar) ---- */
        /* Calculate how many lines fit in the log area.
         * Available height = panel_h - status_bar - input_line - padding. */
        float log_h = panel_h - (float)THEME_ROW_HEIGHT
                      - (float)(THEME_ROW_HEIGHT + 4)
                      - (float)(THEME_PADDING_SMALL * 2);
        int visible = (int)(log_h / (float)(THEME_ROW_HEIGHT + 1));
        if (visible < 1) visible = 1;
        ed->ui.tui_log_visible = visible;

        int count = ed->ui.tui_log_count;
        int scroll = ed->ui.tui_log_scroll;
        int tui_max_scroll = count - visible;
        if (tui_max_scroll < 0) tui_max_scroll = 0;
        if (scroll > tui_max_scroll) scroll = tui_max_scroll;
        if (scroll < 0) scroll = 0;
        ed->ui.tui_log_scroll = scroll;

        /* Start index: newest is at head-1, scroll back by scroll lines. */
        int first = count - visible - scroll;
        if (first < 0) first = 0;
        int render_count = count - first - scroll;
        if (render_count > visible) render_count = visible;
        if (render_count < 0) render_count = 0;

        int oldest = (ed->ui.tui_log_head - count + UI_TUI_LOG_MAX)
                     % UI_TUI_LOG_MAX;
        bool tui_needs_scrollbar = count > visible;

        CLAY(CLAY_ID("TUI_LogArea"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
            },
        }) {
            /* ---- Log rows ---- */
            Clay_String log_id = {.length = 7, .chars = "TUI_Log"};
            CLAY(CLAY_ID("TUI_Log"), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
                    .layoutDirection = CLAY_TOP_TO_BOTTOM,
                    .childGap = 1,
                },
            }) {
                for (int i = 0; i < render_count; i++) {
                    int idx = (oldest + first + i) % UI_TUI_LOG_MAX;
                    int32_t line_len = (int32_t)strlen(ed->ui.tui_log[idx]);
                    if (line_len == 0) continue;

                    Clay_String line_str = {
                        .length = line_len,
                        .chars = ed->ui.tui_log[idx],
                    };
                    uint8_t line_type = ed->ui.tui_log_type[idx];
                    Clay_Color line_color = {THEME_TEXT_R, THEME_TEXT_G,
                                             THEME_TEXT_B, THEME_TEXT_A};
                    if (line_type == UI_TUI_LOG_ERROR)
                        line_color = (Clay_Color){220, 60, 60, 255};

                    CLAY(CLAY_SIDI(log_id, (uint32_t)i), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0),
                                       CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                            .layoutDirection = CLAY_LEFT_TO_RIGHT,
                            .padding = {THEME_PADDING_SMALL,
                                        THEME_PADDING_SMALL, 0, 0},
                            .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                        },
                    }) {
                        CLAY(CLAY_IDI("TLogTxt", (uint32_t)i), {
                            .layout = {
                                .sizing = {CLAY_SIZING_GROW(0),
                                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                            },
                        }) {
                            Clay__OpenTextElement(line_str,
                                CLAY_TEXT_CONFIG({
                                    .fontSize = THEME_FONT_SIZE_UI,
                                    .textColor = line_color,
                                    .fontId = CLAY_FONT_MONO,
                                }));
                        }

                        if (line_type == UI_TUI_LOG_SUCCESS) {
                            Clay_String ok_str = {.length = 2, .chars = "ok"};
                            CLAY(CLAY_IDI("TLogOk", (uint32_t)i), {
                                .layout = {
                                    .sizing = {CLAY_SIZING_FIT(0),
                                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                                    .padding = {THEME_PADDING_SMALL,
                                                THEME_PADDING_SMALL, 0, 0},
                                    .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                },
                            }) {
                                Clay__OpenTextElement(ok_str,
                                    CLAY_TEXT_CONFIG({
                                        .fontSize = THEME_FONT_SIZE_UI,
                                        .textColor = {60, 200, 60, 255},
                                        .fontId = CLAY_FONT_MONO,
                                    }));
                            }
                        } else if (line_type == UI_TUI_LOG_ERROR) {
                            Clay_String err_str = {.length = 1, .chars = "x"};
                            CLAY(CLAY_IDI("TLogErr", (uint32_t)i), {
                                .layout = {
                                    .sizing = {CLAY_SIZING_FIT(0),
                                               CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                                    .padding = {THEME_PADDING_SMALL,
                                                THEME_PADDING_SMALL, 0, 0},
                                    .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                },
                            }) {
                                Clay__OpenTextElement(err_str,
                                    CLAY_TEXT_CONFIG({
                                        .fontSize = THEME_FONT_SIZE_UI,
                                        .textColor = {220, 60, 60, 255},
                                        .fontId = CLAY_FONT_MONO,
                                    }));
                            }
                        } else if (line_type == UI_TUI_LOG_PENDING) {
                            /* Animated ellipsis: hidden for first 100ms,
                             * then cycles .  ..  ... every 80ms. */
                            uint32_t age = now_ms - ed->ui.tui_log_timestamp[idx];
                            const char *pend_text = "";
                            int32_t pend_len = 0;
                            if (age >= 100) {
                                uint32_t phase = ((age - 100) / 80) % 3;
                                if (phase == 0) { pend_text = ".";   pend_len = 1; }
                                else if (phase == 1) { pend_text = "..";  pend_len = 2; }
                                else { pend_text = "..."; pend_len = 3; }
                            }
                            if (pend_len > 0) {
                                Clay_String pend_str = {.length = pend_len,
                                                        .chars = pend_text};
                                CLAY(CLAY_IDI("TLogPnd", (uint32_t)i), {
                                    .layout = {
                                        .sizing = {CLAY_SIZING_FIT(0),
                                                   CLAY_SIZING_FIXED(THEME_ROW_HEIGHT)},
                                        .padding = {THEME_PADDING_SMALL,
                                                    THEME_PADDING_SMALL, 0, 0},
                                        .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
                                    },
                                }) {
                                    Clay__OpenTextElement(pend_str,
                                        CLAY_TEXT_CONFIG({
                                            .fontSize = THEME_FONT_SIZE_UI,
                                            .textColor = {180, 180, 60, 255},
                                            .fontId = CLAY_FONT_MONO,
                                        }));
                                }
                            }
                        }
                    }
                }
            }

            /* ---- TUI scrollbar ---- */
            if (tui_needs_scrollbar) {
                float thumb_ratio = (float)visible / (float)count;
                if (thumb_ratio > 1.0f) thumb_ratio = 1.0f;
                float thumb_h = log_h * thumb_ratio;
                if (thumb_h < 12.0f) thumb_h = 12.0f;

                float scroll_range = log_h - thumb_h;
                float thumb_offset = 0.0f;
                if (tui_max_scroll > 0) {
                    /* Scroll=0 means at bottom (newest), max_scroll=at top. */
                    thumb_offset = scroll_range
                                   * (1.0f - (float)scroll
                                      / (float)tui_max_scroll);
                }

                CLAY(CLAY_ID("TUI_ScrollTrack"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIXED(8),
                                   CLAY_SIZING_GROW(0)},
                    },
                    .backgroundColor = {25, 27, 33, 255},
                }) {
                    CLAY(CLAY_ID("TUI_ScrollThumb"), {
                        .layout = {
                            .sizing = {CLAY_SIZING_FIXED(8),
                                       CLAY_SIZING_FIXED(thumb_h)},
                        },
                        .backgroundColor = {80, 85, 95, 255},
                        .cornerRadius = CLAY_CORNER_RADIUS(4),
                        .floating = {
                            .attachTo = CLAY_ATTACH_TO_PARENT,
                            .offset = {0, thumb_offset},
                        },
                    }) {}
                }
            }
        }

        /* ---- Input line ---- */
        Clay_Color input_text_color = ed->ui.tui_active
            ? (Clay_Color){THEME_TEXT_R, THEME_TEXT_G,
                           THEME_TEXT_B, THEME_TEXT_A}
            : (Clay_Color){THEME_TEXT_DIM_R, THEME_TEXT_DIM_G,
                           THEME_TEXT_DIM_B, THEME_TEXT_DIM_A};
        CLAY(CLAY_ID("TUI_Input"), {
            .layout = {
                .sizing = {CLAY_SIZING_GROW(0),
                           CLAY_SIZING_FIXED(THEME_ROW_HEIGHT + 4)},
                .padding = {THEME_PADDING_SMALL, THEME_PADDING_SMALL,
                            2, 2},
                .layoutDirection = CLAY_LEFT_TO_RIGHT,
                .childAlignment = {.y = CLAY_ALIGN_Y_CENTER},
            },
            .backgroundColor = ed->ui.tui_active
                ? (Clay_Color){40, 42, 48, 255}
                : (Clay_Color){30, 32, 38, 255},
        }) {
            if (ed->ui.tui_active) {
                /* Pre-cursor text. */
                Clay_String pre_str = {
                    .length = (int32_t)strlen(s_input_pre),
                    .chars = s_input_pre,
                };
                Clay__OpenTextElement(pre_str,
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = input_text_color,
                        .fontId = CLAY_FONT_MONO,
                    }));

                /* Cursor: character + underline bar beneath.
                 * Use a fixed-height container so the bar position
                 * is consistent whether or not a char is visible. */
                CLAY(CLAY_ID("TUI_Cursor"), {
                    .layout = {
                        .sizing = {CLAY_SIZING_FIT(0),
                                   CLAY_SIZING_FIXED(THEME_FONT_SIZE_UI + 4)},
                        .layoutDirection = CLAY_TOP_TO_BOTTOM,
                        .childAlignment = {.y = CLAY_ALIGN_Y_BOTTOM},
                    },
                }) {
                    Clay_String cur_str = {
                        .length = (int32_t)strlen(s_input_cur),
                        .chars = s_input_cur,
                    };
                    Clay__OpenTextElement(cur_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = input_text_color,
                            .fontId = CLAY_FONT_MONO,
                        }));
                    /* Underline bar. */
                    CLAY(CLAY_ID("TUI_CursorBar"), {
                        .layout = {
                            .sizing = {CLAY_SIZING_GROW(0),
                                       CLAY_SIZING_FIXED(2)},
                        },
                        .backgroundColor = {THEME_TEXT_R, THEME_TEXT_G,
                                             THEME_TEXT_B, THEME_TEXT_A},
                    }) {}
                }

                /* Post-cursor text. */
                if (s_input_post[0] != '\0') {
                    Clay_String post_str = {
                        .length = (int32_t)strlen(s_input_post),
                        .chars = s_input_post,
                    };
                    Clay__OpenTextElement(post_str,
                        CLAY_TEXT_CONFIG({
                            .fontSize = THEME_FONT_SIZE_UI,
                            .textColor = input_text_color,
                            .fontId = CLAY_FONT_MONO,
                        }));
                }
            } else {
                Clay_String input_str = {
                    .length = (int32_t)strlen(s_input_buf),
                    .chars = s_input_buf,
                };
                Clay__OpenTextElement(input_str,
                    CLAY_TEXT_CONFIG({
                        .fontSize = THEME_FONT_SIZE_UI,
                        .textColor = input_text_color,
                        .fontId = CLAY_FONT_MONO,
                    }));
            }
        }
    }
}
