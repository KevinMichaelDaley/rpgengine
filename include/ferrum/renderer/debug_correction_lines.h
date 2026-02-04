#ifndef FERRUM_RENDERER_DEBUG_CORRECTION_LINES_H
#define FERRUM_RENDERER_DEBUG_CORRECTION_LINES_H

#include <stddef.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/** @file
 * @brief Helpers for generating debug geometry for client/server corrections.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Generate line vertices showing correction from estimated cube to authoritative cube.
 *
 * Produces 8 line segments (16 vertices) connecting corresponding corners.
 * Output format: [est_corner0, true_corner0, est_corner1, true_corner1, ...].
 *
 * @param est_pos Estimated position.
 * @param est_rot Estimated rotation.
 * @param true_pos Authoritative position.
 * @param true_rot Authoritative rotation.
 * @param half_extent Half-size of the cube in world units.
 * @param out_vertices Output vertex buffer (non-NULL).
 * @param out_vertices_cap Capacity in vec3_t units.
 * @return Number of vertices written (0 on error or insufficient capacity).
 */
size_t fr_debug_correction_lines_cube(vec3_t est_pos,
                                     quat_t est_rot,
                                     vec3_t true_pos,
                                     quat_t true_rot,
                                     float half_extent,
                                     vec3_t *out_vertices,
                                     size_t out_vertices_cap);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_DEBUG_CORRECTION_LINES_H */
