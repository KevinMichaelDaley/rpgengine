/**
 * @file tui_panel_render.c
 * @brief TUI panel Clay rendering — status bar, log area, input line.
 */

#include "ferrum/editor/ui/tui_panel.h"
#include "clay.h"

#include <stdio.h>
#include <string.h>

/** @brief Create a Clay_String from a dynamic (non-literal) C string. */
#define CLAY_DYNAMIC_STRING(str, len) \
    ((Clay_String){.isStaticallyAllocated = false, .length = (len), .chars = (str)})

/* ---- Color constants for log levels ---- */

static const Clay_Color COLOR_BG    = {20, 20, 30, 255};
static const Clay_Color COLOR_TEXT  = {200, 200, 200, 255};
static const Clay_Color COLOR_WARN  = {255, 200, 50, 255};
static const Clay_Color COLOR_ERR   = {255, 80, 80, 255};
static const Clay_Color COLOR_STATUS_BAR = {40, 40, 60, 255};
static const Clay_Color COLOR_INPUT_BG   = {30, 30, 50, 255};

/** @brief Choose text color based on log level. */
static Clay_Color color_for_level(uint8_t level) {
    switch (level) {
        case 1: return COLOR_WARN;
        case 2: return COLOR_ERR;
        default: return COLOR_TEXT;
    }
}

/** @brief Render the status bar at the top of the TUI panel. */
static void render_status_bar(tui_panel_t *panel, float width) {
    char status_text[256];
    tui_panel_format_status(panel, status_text, sizeof(status_text));

    CLAY(CLAY_ID("TUI_StatusBar"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(20)},
            .padding = {4, 4, 2, 2},
        },
        .backgroundColor = COLOR_STATUS_BAR,
    }) {
        CLAY_TEXT(CLAY_DYNAMIC_STRING(status_text, (int32_t)strlen(status_text)),
                  CLAY_TEXT_CONFIG({
                      .fontSize = 14,
                      .textColor = COLOR_TEXT,
                  }));
    }
}

/** @brief Render the log entries in the scrollback area. */
static void render_log_area(tui_panel_t *panel, float width, float height) {
    uint32_t visible = ctrl_log_visible_count(&panel->tui.log);

    /* Calculate max visible rows (approx 16px per line) */
    int max_rows = (int)(height / 16.0f);
    if (max_rows < 1) max_rows = 1;
    uint32_t rows_to_show = (visible < (uint32_t)max_rows)
                                ? visible : (uint32_t)max_rows;

    CLAY(CLAY_ID("TUI_LogArea"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_GROW(0)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
            .padding = {4, 4, 2, 2},
        },
        .backgroundColor = COLOR_BG,
    }) {
        /* Render from oldest visible to newest (top to bottom) */
        for (uint32_t i = 0; i < rows_to_show; i++) {
            /* Index from bottom: rows_to_show - 1 - i gives top-to-bottom */
            uint32_t log_idx = rows_to_show - 1 - i;
            const ctrl_log_entry_t *entry =
                ctrl_log_get(&panel->tui.log, log_idx);
            if (!entry) continue;

            Clay_Color text_color = color_for_level(entry->level);
            int32_t text_len = (int32_t)strlen(entry->text);

            CLAY(CLAY_IDI("TUI_LogLine", (uint32_t)i), {
                .layout = {
                    .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_FIXED(16)},
                },
            }) {
                CLAY_TEXT(
                    CLAY_DYNAMIC_STRING(entry->text, text_len),
                    CLAY_TEXT_CONFIG({
                        .fontSize = 14,
                        .textColor = text_color,
                    }));
            }
        }
    }
}

/** @brief Render the command input line at the bottom. */
static void render_input_line(tui_panel_t *panel, float width) {
    /* Build display text: ":" prefix + command text in command mode,
     * or empty prompt in normal mode */
    char display[CTRL_CMD_MAX_LEN + 2];
    if (panel->tui.mode == CTRL_MODE_COMMAND) {
        snprintf(display, sizeof(display), ":%s", panel->tui.cmd_text);
    } else {
        display[0] = '\0';
    }

    int32_t display_len = (int32_t)strlen(display);

    CLAY(CLAY_ID("TUI_InputLine"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(width), CLAY_SIZING_FIXED(20)},
            .padding = {4, 4, 2, 2},
        },
        .backgroundColor = COLOR_INPUT_BG,
    }) {
        if (display_len > 0) {
            CLAY_TEXT(
                CLAY_DYNAMIC_STRING(display, display_len),
                CLAY_TEXT_CONFIG({
                    .fontSize = 14,
                    .textColor = COLOR_TEXT,
                }));
        }
    }
}

void tui_panel_render_clay(tui_panel_t *panel, const tui_panel_rect_t *rect) {
    if (!panel || !panel->initialized || !rect) return;

    float log_height = rect->height - 40.0f; /* minus status + input */
    if (log_height < 0) log_height = 0;

    CLAY(CLAY_ID("TUI_Panel"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(rect->width),
                       CLAY_SIZING_FIXED(rect->height)},
            .layoutDirection = CLAY_TOP_TO_BOTTOM,
        },
        .backgroundColor = COLOR_BG,
    }) {
        render_status_bar(panel, rect->width);
        render_log_area(panel, rect->width, log_height);
        render_input_line(panel, rect->width);
    }
}
