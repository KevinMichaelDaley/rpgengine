/**
 * @file client_cursor.h
 * @brief 3D editor cursor state and rendering.
 *
 * The cursor is a visual indicator in the 3D viewport showing where
 * editor operations will take place. Rendered as three axis-colored lines
 * (X=red, Y=green, Z=blue) with a grid cell highlight on the XZ plane.
 *
 * Cursor state is owned by the client, updated by commands from the
 * controller process via the client state socket.
 *
 * Only compiled when EDITOR_ENABLE is defined.
 *
 * Thread safety: single-threaded (client render thread only).
 */
#ifndef FERRUM_EDITOR_CLIENT_CURSOR_H
#define FERRUM_EDITOR_CLIENT_CURSOR_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/vec3.h"

/* Forward declaration — avoids pulling in the full header. */
struct fr_debug_lines;

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief 3D editor cursor state.
 *
 * Holds cursor position, grid settings, and visibility.
 * Does not own any heap memory — all state is inline.
 */
typedef struct editor_cursor {
    vec3_t position;       /**< World-space position. */
    float  grid_size;      /**< Grid cell size for snap (metres). */
    bool   snap_enabled;   /**< Whether grid snap is active. */
    bool   visible;        /**< Whether the cursor is drawn. */
} editor_cursor_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize cursor to default state.
 *
 * Position = origin, grid_size = 1.0, snap = true, visible = true.
 *
 * @param cursor  Cursor to initialize (non-NULL).
 */
void editor_cursor_init(editor_cursor_t *cursor);

/* ------------------------------------------------------------------------ */
/* Mutation                                                                  */
/* ------------------------------------------------------------------------ */

/**
 * @brief Move cursor by a delta in world space.
 *
 * If snap is enabled, the resulting position is snapped to the grid.
 *
 * @param cursor  Cursor (non-NULL).
 * @param delta   World-space displacement to add.
 */
void editor_cursor_move(editor_cursor_t *cursor, vec3_t delta);

/**
 * @brief Set cursor position to an absolute world-space location.
 *
 * If snap is enabled, the position is snapped to the grid.
 *
 * @param cursor  Cursor (non-NULL).
 * @param pos     Desired world-space position.
 */
void editor_cursor_set_position(editor_cursor_t *cursor, vec3_t pos);

/**
 * @brief Toggle cursor visibility.
 * @param cursor  Cursor (non-NULL).
 */
void editor_cursor_toggle_visible(editor_cursor_t *cursor);

/* ------------------------------------------------------------------------ */
/* Rendering                                                                 */
/* ------------------------------------------------------------------------ */

/**
 * @brief Submit cursor debug lines for rendering.
 *
 * Pushes axis lines and grid-cell highlight into the debug line store.
 * Call once per frame before collecting debug line vertices.
 *
 * Lines added (each with ttl of one frame):
 * - X axis: red line from (pos.x - grid_size, pos.y, pos.z)
 *                      to (pos.x + grid_size, pos.y, pos.z)
 * - Y axis: green line on Y
 * - Z axis: blue line on Z
 * - Grid highlight: 4 lines forming a square on the XZ plane at pos.y
 *
 * @param cursor   Cursor (non-NULL).
 * @param lines    Debug line store to push into (non-NULL).
 * @param now_s    Current time in seconds.
 * @param frame_dt Frame delta time in seconds (used as TTL).
 * @return Number of lines submitted (0 if cursor is hidden).
 */
uint32_t editor_cursor_submit_lines(const editor_cursor_t *cursor,
                                    struct fr_debug_lines *lines,
                                    double now_s,
                                    double frame_dt);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_CLIENT_CURSOR_H */
