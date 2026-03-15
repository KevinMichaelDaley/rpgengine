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

#include "ferrum/math/vec3.h"

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
 * scale = 1.0. All axes enabled.
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

/* ---- Gizmo integration helpers ---- */

/**
 * @brief Snap an absolute position to the grid, return corrected delta.
 *
 * Computes target = origin + accum_delta, snaps each axis to the grid,
 * then returns (snapped_target - origin) as the corrected delta.
 * If snapping is disabled, returns accum_delta unchanged.
 *
 * @param snap        Snap state (non-NULL).
 * @param origin      Entity position at drag start.
 * @param accum_delta Accumulated raw drag delta so far.
 * @return Corrected delta that places the entity on the grid.
 */
vec3_t snap_apply_position(const snap_state_t *snap,
                            vec3_t origin, vec3_t accum_delta);

/**
 * @brief Snap an accumulated rotation angle (degrees) to the grid.
 *
 * If snapping is disabled, returns the angle unchanged.
 *
 * @param snap  Snap state (non-NULL).
 * @param angle_deg  Accumulated rotation in degrees.
 * @param axis  Axis index (0=X, 1=Y, 2=Z).
 * @return Snapped angle in degrees.
 */
float snap_apply_rotation(const snap_state_t *snap, float angle_deg, int axis);

/**
 * @brief Snap absolute scale to the grid, return corrected scale factor.
 *
 * Computes target_scale = orig_scale * accum_factor (per-axis),
 * snaps each axis to the scale grid, then returns the corrected
 * multiplicative factor (snapped / orig_scale).
 * If snapping is disabled, returns accum_factor unchanged.
 *
 * @param snap         Snap state (non-NULL).
 * @param orig_scale   Entity scale at drag start.
 * @param accum_factor Accumulated multiplicative scale factor.
 * @return Corrected scale factor that places the entity on the grid.
 */
vec3_t snap_apply_scale(const snap_state_t *snap,
                         vec3_t orig_scale, vec3_t accum_factor);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_SNAP_STATE_H */
