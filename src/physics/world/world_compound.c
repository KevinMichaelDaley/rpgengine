/**
 * @file world_compound.c
 * @brief Compound convex collider registration in the physics world.
 *
 * Provides a single function to register a decomposed mesh as a
 * compound convex collider on a body.  The decompose result's hulls
 * are copied into the world's convex_hulls pool, and a compound
 * shape entry tracks which hull indices belong together.
 *
 * Non-static functions (1):
 *   1. phys_world_set_compound_collider
 */

#include "ferrum/physics/world.h"
#include "ferrum/physics/convex_decompose.h"
#include "ferrum/physics/convex_compound.h"

#include <string.h>

void phys_world_set_compound_collider(phys_world_t *world,
                                       uint32_t body_index,
                                       const phys_decompose_result_t *result,
                                       phys_vec3_t offset) {
    if (!world || !result || result->hull_count == 0) return;

    /* Allocate a compound shape slot. */
    uint32_t ci = world->compound_count++;
    phys_convex_compound_t *cc = &world->compounds[ci];
    memset(cc, 0, sizeof(*cc));

    /* Copy each hull into the world's convex_hulls pool and record
     * the index in the compound. */
    uint32_t n = result->hull_count;
    if (n > PHYS_COMPOUND_MAX_CHILDREN) n = PHYS_COMPOUND_MAX_CHILDREN;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t hi = world->convex_hull_count++;
        world->convex_hulls[hi] = result->hulls[i];
        cc->child_hull_indices[i] = hi;
    }
    cc->child_count = n;

    /* Set the body's collider to COMPOUND. */
    phys_collider_t *col = &world->colliders[body_index];
    col->type = PHYS_SHAPE_COMPOUND;
    col->shape_index = ci;
    col->local_offset = offset;
    col->local_rotation = (phys_quat_t){0, 0, 0, 1};

    /* Invalidate static BVH if body is static. */
    if (world->static_bvh_valid &&
        phys_body_is_sleeping(&world->body_pool.bodies_curr[body_index])) {
        world->static_bvh_valid = 0;
    }
}
