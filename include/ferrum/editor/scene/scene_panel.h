/**
 * @file scene_panel.h
 * @brief Panel layout engine for the scene editor.
 *
 * Manages four panel regions (Outliner, Viewport, Inspector, TUI)
 * with draggable dividers, toggle visibility, and focus tracking.
 *
 * Ownership: caller owns the panel_layout_t; no dynamic allocation.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: invalid panel/divider IDs are silently ignored.
 * Side effects: none (pure layout computation).
 *
 * Public types: panel_layout_t, panel_rect_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PANEL_H
#define FERRUM_EDITOR_SCENE_PANEL_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** Minimum panel dimension in pixels. */
#define PANEL_MIN_SIZE 40

/* ---- Enumerations (forward-declared, not counted toward 2-type rule) ---- */

/**
 * @brief Panel identifiers.
 *
 * Layout:
 *   OUTLINER (left) | VIEWPORT (center-top)   | INSPECTOR (right)
 *                   | TUI (center-bottom)      |
 */
typedef enum panel_id {
    PANEL_OUTLINER  = 0,
    PANEL_VIEWPORT  = 1,
    PANEL_INSPECTOR = 2,
    PANEL_TUI       = 3,
    PANEL_COUNT     = 4
} panel_id_t;

/**
 * @brief Divider identifiers for drag operations.
 */
typedef enum divider_id {
    DIVIDER_NONE   = -1,
    DIVIDER_LEFT   = 0,  /**< Between outliner and viewport/TUI. */
    DIVIDER_RIGHT  = 1,  /**< Between viewport/TUI and inspector. */
    DIVIDER_BOTTOM = 2,  /**< Between viewport and TUI (horizontal). */
    DIVIDER_COUNT  = 3
} divider_id_t;

/* ---- Public types ---- */

/**
 * @brief Rectangle describing a panel's screen region.
 */
typedef struct panel_rect {
    int x;   /**< Left edge in pixels. */
    int y;   /**< Top edge in pixels. */
    int w;   /**< Width in pixels. */
    int h;   /**< Height in pixels. */
} panel_rect_t;

/**
 * @brief Panel layout state.
 *
 * Stores window dimensions, divider positions (as fractions of window
 * size), panel visibility, and focus state. All panel rects are
 * recomputed on demand from these values.
 */
typedef struct panel_layout {
    int window_w;  /**< Window width in pixels. */
    int window_h;  /**< Window height in pixels. */

    /**
     * Divider positions as fractions [0,1] of the relevant dimension.
     * [DIVIDER_LEFT]   = fraction of window_w for left divider.
     * [DIVIDER_RIGHT]  = fraction of window_w for right divider.
     * [DIVIDER_BOTTOM] = fraction of window_h for horizontal divider
     *                     (measured from top).
     */
    float divider_pos[DIVIDER_COUNT];

    bool visible[PANEL_COUNT];   /**< Per-panel visibility. */
    panel_id_t focus;            /**< Currently focused panel. */
} panel_layout_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize a panel layout with default divider positions.
 *
 * Default dividers: left at 15% width, right at 80% width,
 * bottom at 65% height. All panels visible, focus on viewport.
 *
 * @param layout  Layout to initialize (non-NULL).
 * @param window_w  Window width in pixels.
 * @param window_h  Window height in pixels.
 */
void panel_layout_init(panel_layout_t *layout, int window_w, int window_h);

/* ---- Queries ---- */

/**
 * @brief Compute the screen rectangle for a panel.
 *
 * Hidden panels return a zero-area rect.
 *
 * @param layout  Layout (non-NULL).
 * @param id      Panel to query.
 * @return Panel rectangle in pixel coordinates.
 */
panel_rect_t panel_layout_get_rect(const panel_layout_t *layout, panel_id_t id);

/**
 * @brief Check whether a panel is visible.
 * @param layout  Layout (non-NULL).
 * @param id      Panel to query.
 * @return true if visible.
 */
bool panel_layout_is_visible(const panel_layout_t *layout, panel_id_t id);

/**
 * @brief Get the currently focused panel.
 * @param layout  Layout (non-NULL).
 * @return The focused panel ID.
 */
panel_id_t panel_layout_get_focus(const panel_layout_t *layout);

/**
 * @brief Hit-test: which panel contains this point?
 * @param layout  Layout (non-NULL).
 * @param x  Screen X coordinate.
 * @param y  Screen Y coordinate.
 * @return Panel ID, or PANEL_VIEWPORT as fallback.
 */
panel_id_t panel_layout_hit_test(const panel_layout_t *layout, int x, int y);

/**
 * @brief Hit-test: which divider is near this point?
 * @param layout  Layout (non-NULL).
 * @param x  Screen X coordinate.
 * @param y  Screen Y coordinate.
 * @return Divider ID, or DIVIDER_NONE if not near any divider.
 */
divider_id_t panel_layout_divider_hit_test(const panel_layout_t *layout,
                                            int x, int y);

/* ---- Mutations ---- */

/**
 * @brief Handle a window resize.
 *
 * Divider positions (stored as fractions) are preserved; panel rects
 * scale to the new window size.
 *
 * @param layout  Layout (non-NULL).
 * @param new_w   New window width.
 * @param new_h   New window height.
 */
void panel_layout_resize(panel_layout_t *layout, int new_w, int new_h);

/**
 * @brief Drag a divider by a pixel delta.
 *
 * The divider position is clamped so that adjacent panels respect
 * PANEL_MIN_SIZE.
 *
 * @param layout  Layout (non-NULL).
 * @param div     Which divider to drag.
 * @param delta   Pixel delta (positive = right/down).
 */
void panel_layout_drag_divider(panel_layout_t *layout, divider_id_t div,
                                int delta);

/**
 * @brief Toggle a panel's visibility.
 *
 * When a panel is hidden, its space is redistributed to neighbors.
 * If the focused panel is hidden, focus moves to viewport.
 *
 * @param layout  Layout (non-NULL).
 * @param id      Panel to toggle.
 */
void panel_layout_toggle(panel_layout_t *layout, panel_id_t id);

/* ---- Focus ---- */

/**
 * @brief Set focus to a specific panel (click-to-focus).
 *
 * Ignored if the target panel is hidden.
 *
 * @param layout  Layout (non-NULL).
 * @param id      Panel to focus.
 */
void panel_layout_set_focus(panel_layout_t *layout, panel_id_t id);

/**
 * @brief Cycle focus to the next visible panel (Tab).
 * @param layout  Layout (non-NULL).
 */
void panel_layout_focus_next(panel_layout_t *layout);

/**
 * @brief Cycle focus to the previous visible panel (Shift+Tab).
 * @param layout  Layout (non-NULL).
 */
void panel_layout_focus_prev(panel_layout_t *layout);

/**
 * @brief Return focus to the viewport (Escape).
 * @param layout  Layout (non-NULL).
 */
void panel_layout_focus_viewport(panel_layout_t *layout);

/* ---- Persistence ---- */

/**
 * @brief Save panel layout to a config file.
 *
 * Writes divider positions and visibility flags as a simple text file.
 *
 * @param layout  Layout (non-NULL).
 * @param path    File path to write.
 * @return true on success.
 */
bool panel_layout_save(const panel_layout_t *layout, const char *path);

/**
 * @brief Load panel layout from a config file.
 *
 * Restores divider positions and visibility. Window dimensions
 * are NOT loaded (caller must set those separately).
 *
 * @param layout  Layout (non-NULL, must be initialized first).
 * @param path    File path to read.
 * @return true on success, false if file doesn't exist or is invalid.
 */
bool panel_layout_load(panel_layout_t *layout, const char *path);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PANEL_H */
