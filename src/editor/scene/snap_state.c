/**
 * @file snap_state.c
 * @brief Grid/snap state implementation.
 */

#include "ferrum/editor/scene/snap_state.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <string.h>

void snap_state_init(snap_state_t *snap) {
    memset(snap, 0, sizeof(*snap));

    /* All snaps disabled by default */
    for (int i = 0; i < SNAP_TYPE_COUNT; ++i) {
        snap->enabled[i] = false;
        snap->axis_x[i] = true;
        snap->axis_y[i] = true;
        snap->axis_z[i] = true;
    }

    /* Default grid sizes */
    snap->grid_size[SNAP_POSITION] = 1.0f;
    snap->grid_size[SNAP_ROTATION] = 15.0f;
    snap->grid_size[SNAP_SCALE]    = 1.0f;
}

float snap_state_quantize(const snap_state_t *snap, snap_transform_type_t type,
                           float value, int axis) {
    if (type < 0 || type >= SNAP_TYPE_COUNT) return value;
    if (!snap->enabled[type]) return value;

    /* Check per-axis toggle */
    bool axis_enabled = false;
    switch (axis) {
    case 0: axis_enabled = snap->axis_x[type]; break;
    case 1: axis_enabled = snap->axis_y[type]; break;
    case 2: axis_enabled = snap->axis_z[type]; break;
    default: return value;
    }
    if (!axis_enabled) return value;

    float grid = snap->grid_size[type];
    if (grid <= 0.0f) return value;

    return roundf(value / grid) * grid;
}

vec3_t snap_apply_position(const snap_state_t *snap,
                            vec3_t origin, vec3_t accum_delta) {
    if (!snap->enabled[SNAP_POSITION]) return accum_delta;

    /* Compute absolute target, snap it, derive corrected delta. */
    vec3_t target = {
        origin.x + accum_delta.x,
        origin.y + accum_delta.y,
        origin.z + accum_delta.z
    };
    vec3_t snapped = {
        snap_state_quantize(snap, SNAP_POSITION, target.x, 0),
        snap_state_quantize(snap, SNAP_POSITION, target.y, 1),
        snap_state_quantize(snap, SNAP_POSITION, target.z, 2)
    };
    return (vec3_t){
        snapped.x - origin.x,
        snapped.y - origin.y,
        snapped.z - origin.z
    };
}

float snap_apply_rotation(const snap_state_t *snap, float angle_deg, int axis) {
    return snap_state_quantize(snap, SNAP_ROTATION, angle_deg, axis);
}

vec3_t snap_apply_scale(const snap_state_t *snap,
                         vec3_t orig_scale, vec3_t accum_factor) {
    if (!snap->enabled[SNAP_SCALE]) return accum_factor;

    /* Compute absolute target scale, snap it, derive corrected factor. */
    vec3_t target = {
        orig_scale.x * accum_factor.x,
        orig_scale.y * accum_factor.y,
        orig_scale.z * accum_factor.z
    };
    vec3_t snapped = {
        snap_state_quantize(snap, SNAP_SCALE, target.x, 0),
        snap_state_quantize(snap, SNAP_SCALE, target.y, 1),
        snap_state_quantize(snap, SNAP_SCALE, target.z, 2)
    };
    /* Prevent division by zero — if original scale axis is zero,
     * return the raw factor unchanged. */
    return (vec3_t){
        (fabsf(orig_scale.x) > 1e-9f) ? snapped.x / orig_scale.x
                                       : accum_factor.x,
        (fabsf(orig_scale.y) > 1e-9f) ? snapped.y / orig_scale.y
                                       : accum_factor.y,
        (fabsf(orig_scale.z) > 1e-9f) ? snapped.z / orig_scale.z
                                       : accum_factor.z,
    };
}
