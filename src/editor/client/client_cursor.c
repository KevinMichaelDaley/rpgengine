/**
 * @file client_cursor.c
 * @brief 3D editor cursor state management.
 *
 * Non-static functions: init, move, set_position, toggle_visible (4).
 */

#include "ferrum/editor/client/client_cursor.h"

#include <math.h>
#include <stddef.h>

/**
 * @brief Snap a single coordinate to the nearest grid multiple.
 */
static float snap_to_grid_(float val, float grid) {
    return roundf(val / grid) * grid;
}

/**
 * @brief Apply grid snap to a position if enabled.
 */
static vec3_t maybe_snap_(vec3_t pos, float grid, bool snap) {
    if (!snap || grid <= 0.0f) return pos;
    return (vec3_t){
        .x = snap_to_grid_(pos.x, grid),
        .y = snap_to_grid_(pos.y, grid),
        .z = snap_to_grid_(pos.z, grid),
    };
}

void editor_cursor_init(editor_cursor_t *cursor) {
    if (!cursor) return;
    cursor->position     = (vec3_t){0.0f, 0.0f, 0.0f};
    cursor->grid_size    = 1.0f;
    cursor->snap_enabled = true;
    cursor->visible      = true;
}

void editor_cursor_move(editor_cursor_t *cursor, vec3_t delta) {
    if (!cursor) return;
    vec3_t raw = {
        .x = cursor->position.x + delta.x,
        .y = cursor->position.y + delta.y,
        .z = cursor->position.z + delta.z,
    };
    cursor->position = maybe_snap_(raw, cursor->grid_size,
                                   cursor->snap_enabled);
}

void editor_cursor_set_position(editor_cursor_t *cursor, vec3_t pos) {
    if (!cursor) return;
    cursor->position = maybe_snap_(pos, cursor->grid_size,
                                   cursor->snap_enabled);
}

void editor_cursor_toggle_visible(editor_cursor_t *cursor) {
    if (!cursor) return;
    cursor->visible = !cursor->visible;
}
