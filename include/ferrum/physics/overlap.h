#ifndef FERRUM_PHYSICS_OVERLAP_H
#define FERRUM_PHYSICS_OVERLAP_H

/** @file
 * @brief Phase 5.2: Shape overlap query against world bodies.
 */

#include <stdint.h>

#include "ferrum/physics/phys_quat.h"
#include "ferrum/physics/phys_vec3.h"

struct phys_world;
struct phys_collider;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Test a shape at a given pose against all bodies in the world.
 *
 * The @p shape collider reference is interpreted against the world's
 * shape pools (sphere/box/capsule arrays). The shape's local_offset and
 * local_rotation are applied relative to the supplied @p position and
 * @p rotation.
 *
 * @param world         World to query (NULL returns 0).
 * @param shape         Collider reference for the query shape (NULL returns 0).
 * @param position      World-space position for the query shape.
 * @param rotation      World-space rotation for the query shape.
 * @param body_ids_out  Output array of overlapping body ids (NULL returns 0).
 * @param max_results   Capacity of body_ids_out.
 * @param layer_mask    Layer filter mask (see phys_collider_t::layer_id).
 * @return Number of body ids written.
 */
uint32_t phys_overlap(const struct phys_world *world, const struct phys_collider *shape,
                      phys_vec3_t position, phys_quat_t rotation,
                      uint32_t *body_ids_out, uint32_t max_results,
                      uint32_t layer_mask);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_OVERLAP_H */
