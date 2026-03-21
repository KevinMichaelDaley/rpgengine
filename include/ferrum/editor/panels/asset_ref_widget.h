/**
 * @file asset_ref_widget.h
 * @brief Generic asset reference selector widget state management.
 *
 * A reusable widget for selecting assets by path. The widget maintains
 * a path string, a display-friendly filename, focus/confirm state, and
 * an asset type filter. The Clay UI build is in a separate translation
 * unit (asset_ref_widget_build.c).
 *
 * Ownership: caller owns asset_ref_state_t (stack or embedded).
 * Nullability: all functions are NULL-safe on the state pointer.
 * Error semantics: no error returns; invalid args are silent no-ops.
 * Side effects: none (pure state management).
 *
 * Public types: asset_ref_state_t, asset_ref_result_t (2 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_PANELS_ASSET_REF_WIDGET_H
#define FERRUM_EDITOR_PANELS_ASSET_REF_WIDGET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Maximum path length for asset references. */
#define ASSET_REF_PATH_MAX 256

/** @brief Maximum display name length. */
#define ASSET_REF_DISPLAY_MAX 64

/**
 * @brief Asset reference selector widget state.
 *
 * Stores the selected asset path, a display-friendly filename extracted
 * from the path, focus/confirm flags, and an asset type filter.
 */
typedef struct asset_ref_state {
    char     path[ASSET_REF_PATH_MAX];       /**< Full asset path. */
    char     display[ASSET_REF_DISPLAY_MAX]; /**< Filename portion for display. */
    bool     focused;                        /**< True if widget has input focus. */
    bool     confirmed;                      /**< True if user confirmed selection. */
    uint8_t  filter_type;                    /**< Asset type filter (0 = any). */
} asset_ref_state_t;

/**
 * @brief Per-frame result from the Clay UI build.
 */
typedef struct asset_ref_result {
    bool        changed;    /**< True if path changed this frame. */
    bool        confirmed;  /**< True if user confirmed this frame. */
    const char *path;       /**< Current path (points into state). */
} asset_ref_result_t;

/* ---- Lifecycle (asset_ref_widget.c) ---- */

/**
 * @brief Initialize widget state to defaults.
 *
 * Zeroes all fields and sets the asset type filter.
 *
 * @param state        Widget state (NULL-safe: no-op if NULL).
 * @param filter_type  Asset type filter (0 = accept any type).
 */
void asset_ref_init(asset_ref_state_t *state, uint8_t filter_type);

/**
 * @brief Set the current asset path.
 *
 * Copies the path into state and extracts the filename portion
 * into the display field. If path is NULL, both fields are cleared.
 * Paths longer than ASSET_REF_PATH_MAX-1 are truncated.
 *
 * @param state  Widget state (NULL-safe: no-op if NULL).
 * @param path   Asset path to set, or NULL to clear.
 */
void asset_ref_set_path(asset_ref_state_t *state, const char *path);

/**
 * @brief Accept an asset selection from the asset browser.
 *
 * Called when the user clicks an asset in the asset tree while this
 * widget has focus. Sets the path and clears the confirmed flag so
 * the user must re-confirm.
 *
 * @param state  Widget state (NULL-safe: no-op if NULL).
 * @param path   Selected asset path, or NULL to clear.
 */
void asset_ref_accept(asset_ref_state_t *state, const char *path);

/**
 * @brief Confirm the current selection.
 *
 * Sets the confirmed flag. The caller can then read the path and
 * act on it (e.g. set an entity attribute).
 *
 * @param state  Widget state (NULL-safe: no-op if NULL).
 */
void asset_ref_confirm(asset_ref_state_t *state);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_PANELS_ASSET_REF_WIDGET_H */
