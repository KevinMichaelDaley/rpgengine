/**
 * @file snap_state.h
 * @brief Grid/snap state for the scene editor.
 *
 * Per-transform-type snap enable, grid sizes, per-axis toggles.
 * Local to this editor instance — not synced to server.
 *
 * Ownership: caller owns the snap_state_t; no dynamic allocation.
 * Nullability: all pointer params must be non-NULL.
 * Error semantics: invalid indices are silently ignored.
 * Side effects: none (pure state).
 *
 * Public types: snap_state_t, snap_transform_type_t (enum) (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_SNAP_STATE_H
#define FERRUM_EDITOR_SCENE_SNAP_STATE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

/**
 * @brief Transform types that can be independently snapped.
 */
typedef enum snap_transform_type {
    SNAP_POSITION = 0,
    SNAP_ROTATION = 1,
    SNAP_SCALE    = 2,
    SNAP_TYPE_COUNT = 3
} snap_transform_type_t;

/**
 * @brief Per-editor snap/grid state.
 *
 * Each transform type has independent enable and grid size.
 * Per-axis toggles allow snapping only on specific axes.
 */
typedef struct snap_state {
    bool  enabled[SNAP_TYPE_COUNT];     /**< Snap enabled per transform type. */
    float grid_size[SNAP_TYPE_COUNT];   /**< Grid increment per transform type. */
    bool  axis_x[SNAP_TYPE_COUNT];      /**< Per-axis X snap enable. */
    bool  axis_y[SNAP_TYPE_COUNT];      /**< Per-axis Y snap enable. */
    bool  axis_z[SNAP_TYPE_COUNT];      /**< Per-axis Z snap enable. */
} snap_state_t;

/* ---- Lifecycle ---- */

/**
 * @brief Initialize snap state with defaults.
 *
 * All snaps disabled. Position grid = 1.0, rotation = 15.0 degrees,
 * scale = 0.1. All axes enabled.
 *
 * @param snap  State to initialize (non-NULL).
 */
void snap_state_init(snap_state_t *snap);

/* ---- Queries ---- */

/**
 * @brief Snap a value to the grid for a given transform type.
 *
 * If snapping is disabled for this type, returns the value unchanged.
 * Respects per-axis toggles (axis parameter: 0=X, 1=Y, 2=Z).
 *
 * @param snap  Snap state (non-NULL).
 * @param type  Transform type.
 * @param value Value to snap.
 * @param axis  Axis index (0=X, 1=Y, 2=Z).
 * @return Snapped value.
 */
float snap_state_quantize(const snap_state_t *snap, snap_transform_type_t type,
                           float value, int axis);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_SNAP_STATE_H */
