#ifndef FERRUM_PHYSICS_CLOSEST_POINT_H
#define FERRUM_PHYSICS_CLOSEST_POINT_H

/** @file
 * @brief Phase 5.3: Closest point query against world bodies.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_quat.h"
#include "ferrum/physics/phys_vec3.h"

struct phys_world;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Find the closest point on any body's surface to @p point.
 *
 * Broadphase culling is performed using the spatial grid.
 *
 * @param world         World to query (NULL returns false).
 * @param point         World-space query point.
 * @param max_distance  Maximum allowed distance from @p point to the surface.
 *                      If negative, returns false.
 * @param closest_out   Output closest point on surface (required).
 * @param body_id_out   Output body id hit (required).
 * @param layer_mask    Layer filter mask (see phys_collider_t::layer_id).
 * @return true if a body was found within max_distance, else false.
 */
bool phys_closest_point(const struct phys_world *world, phys_vec3_t point,
                        float max_distance, phys_vec3_t *closest_out,
                        uint32_t *body_id_out, uint32_t layer_mask);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CLOSEST_POINT_H */
