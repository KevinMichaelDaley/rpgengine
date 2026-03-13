/**
 * @file tui_panel.h
 * @brief Embedded TUI panel — Clay-based rendering of controller TUI state.
 *
 * Wraps ctrl_tui_t and provides Clay-based rendering for the scene editor's
 * bottom panel. Reuses the controller's log ring buffer and input state
 * machine; replaces ANSI escape rendering with Clay layout elements.
 *
 * Thread safety: single-threaded (scene editor main loop only).
 */
#ifndef FERRUM_EDITOR_UI_TUI_PANEL_H
#define FERRUM_EDITOR_UI_TUI_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/editor/ctrl_tui.h"

/* ---- Types ---- */

/**
 * @brief Configuration for TUI panel initialization.
 */
typedef struct tui_panel_config {
    uint32_t log_capacity;  /**< Ring buffer capacity (0 = default). */
} tui_panel_config_t;

/**
 * @brief Rectangle describing the panel's screen area.
 */
typedef struct tui_panel_rect {
    float x;      /**< Left edge in pixels. */
    float y;      /**< Top edge in pixels. */
    float width;  /**< Width in pixels. */
    float height; /**< Height in pixels. */
} tui_panel_rect_t;

/**
 * @brief Embedded TUI panel state.
 *
 * Contains the controller TUI context (log + input) and rendering state.
 * Ownership: init() allocates internal resources, destroy() frees them.
 */
typedef struct tui_panel {
    ctrl_tui_t tui;         /**< Controller TUI state (log, input, mode). */
    bool       initialized; /**< True after successful init. */
} tui_panel_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize the TUI panel.
 *
 * @param panel   Panel to initialize (must not be NULL).
 * @param config  Configuration (NULL for defaults).
 * @return true on success.
 *
 * Ownership: caller owns panel; must call tui_panel_destroy().
 * Nullability: panel must not be NULL; config may be NULL.
 */
bool tui_panel_init(tui_panel_t *panel, const tui_panel_config_t *config);

/**
 * @brief Destroy TUI panel and free resources.
 *
 * Safe to call on already-destroyed or NULL panels.
 *
 * @param panel  Panel to destroy (may be NULL).
 */
void tui_panel_destroy(tui_panel_t *panel);

/* ---- Input ---- */

/**
 * @brief Feed a keystroke to the TUI input state machine.
 *
 * @param panel  TUI panel.
 * @param ch     Input character byte.
 * @return Command string to send to server, or NULL if no command produced.
 *
 * Side effects: may modify panel mode, command buffer, numeric prefix.
 */
const char *tui_panel_feed_key(tui_panel_t *panel, char ch);

/* ---- Log ---- */

/**
 * @brief Add a log entry to the TUI panel.
 *
 * @param panel  TUI panel.
 * @param level  Log level (0=info, 1=warn, 2=error).
 * @param text   Message text (truncated to CTRL_LOG_MAX_TEXT-1).
 */
void tui_panel_log(tui_panel_t *panel, uint8_t level, const char *text);

/**
 * @brief Add a command log entry with pending status.
 *
 * @param panel   TUI panel.
 * @param text    Command text.
 * @param cmd_id  Request ID for response matching.
 */
void tui_panel_log_cmd(tui_panel_t *panel, const char *text, uint32_t cmd_id);

/**
 * @brief Scroll the log view.
 *
 * @param panel  TUI panel.
 * @param delta  Lines to scroll (positive = up/history, negative = down).
 */
void tui_panel_scroll(tui_panel_t *panel, int delta);

/* ---- Rendering ---- */

/**
 * @brief Render the TUI panel using Clay layout elements.
 *
 * Must be called between Clay_BeginLayout() and Clay_EndLayout().
 * Renders: status bar, log area with scrollback, command input line.
 *
 * @param panel  TUI panel state.
 * @param rect   Screen rectangle for the panel.
 *
 * Side effects: emits Clay layout elements.
 */
void tui_panel_render_clay(tui_panel_t *panel, const tui_panel_rect_t *rect);

/* ---- Status ---- */

/**
 * @brief Format the status bar text into a buffer.
 *
 * @param panel  TUI panel.
 * @param buf    Output buffer.
 * @param cap    Buffer capacity.
 */
void tui_panel_format_status(const tui_panel_t *panel, char *buf, size_t cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_UI_TUI_PANEL_H */
