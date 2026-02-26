/**
 * @file client_cursor_render.c
 * @brief 3D editor cursor debug line submission.
 *
 * Non-static functions: submit_lines (1).
 *
 * Submits 7 debug lines per frame when the cursor is visible:
 * - 3 axis lines (X=red, Y=green, Z=blue), length = 2 * grid_size
 * - 4 grid-cell highlight edges on the XZ plane
 *
 * Color is not encoded in the debug line store (the renderer applies
 * a uniform color per draw call), so axis coloring is handled by the
 * client render loop which draws each axis line group separately.
 * The line store simply records geometry here.
 */

#include "ferrum/editor/client/client_cursor.h"
#include "ferrum/renderer/debug_lines.h"

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Add an axis line through the cursor position.
 *
 * @param lines   Debug line store.
 * @param pos     Cursor center position.
 * @param half    Half-length of the axis line (= grid_size).
 * @param axis    0=X, 1=Y, 2=Z.
 * @param now_s   Current time.
 * @param ttl     Time-to-live for the line.
 */
static bool add_axis_line_(fr_debug_lines_t *lines, vec3_t pos,
                           float half, int axis,
                           double now_s, double ttl) {
    vec3_t a = pos;
    vec3_t b = pos;

    /* Offset the appropriate axis component. */
    float *a_comp = (axis == 0) ? &a.x : (axis == 1) ? &a.y : &a.z;
    float *b_comp = (axis == 0) ? &b.x : (axis == 1) ? &b.y : &b.z;
    *a_comp -= half;
    *b_comp += half;

    return fr_debug_lines_add(lines, a, b, now_s, ttl);
}

/**
 * @brief Add the 4-edge grid cell highlight on the XZ plane.
 *
 * The square is centered on the cursor position with side = 2 * grid_size.
 */
static uint32_t add_grid_highlight_(fr_debug_lines_t *lines, vec3_t pos,
                                    float half,
                                    double now_s, double ttl) {
    float y = pos.y;
    float x0 = pos.x - half;
    float x1 = pos.x + half;
    float z0 = pos.z - half;
    float z1 = pos.z + half;

    vec3_t corners[4] = {
        {x0, y, z0},   /* bottom-left */
        {x1, y, z0},   /* bottom-right */
        {x1, y, z1},   /* top-right */
        {x0, y, z1},   /* top-left */
    };

    uint32_t count = 0;
    for (int i = 0; i < 4; i++) {
        if (fr_debug_lines_add(lines, corners[i], corners[(i + 1) % 4],
                               now_s, ttl)) {
            count++;
        }
    }
    return count;
}

uint32_t editor_cursor_submit_lines(const editor_cursor_t *cursor,
                                    struct fr_debug_lines *lines,
                                    double now_s,
                                    double frame_dt) {
    if (!cursor || !lines || !cursor->visible) return 0;

    float half = cursor->grid_size;
    double ttl = (frame_dt > 0.0) ? frame_dt : 0.016;

    uint32_t count = 0;

    /* Three axis lines. */
    for (int axis = 0; axis < 3; axis++) {
        if (add_axis_line_((fr_debug_lines_t *)lines, cursor->position,
                           half, axis, now_s, ttl)) {
            count++;
        }
    }

    /* Grid cell highlight on XZ plane. */
    count += add_grid_highlight_((fr_debug_lines_t *)lines, cursor->position,
                                 half, now_s, ttl);

    return count;
}
