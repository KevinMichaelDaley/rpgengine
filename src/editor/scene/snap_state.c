/**
 * @file snap_state.c
 * @brief Grid/snap state implementation.
 */

#include "ferrum/editor/scene/snap_state.h"

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
    snap->grid_size[SNAP_SCALE]    = 0.1f;
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
