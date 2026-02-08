/**
 * @file halo_closure.c
 * @brief Stage 3 implementation — Halo Closure.
 *
 * For each T0 body, computes a velocity-swept AABB, queries the
 * spatial grid for nearby bodies, and promotes eligible neighbors
 * (dynamic, not already T0) to T1.
 */

#include "ferrum/physics/halo_closure.h"

#include <math.h>

#include "ferrum/math/vec3.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/tier_list.h"

/** Maximum number of neighbor results per grid query. */
#define HALO_MAX_QUERY_RESULTS 256u

void phys_stage_halo_closure(const phys_halo_closure_args_t *args) {
    if (!args || !args->bodies || !args->aabbs ||
        !args->grid || !args->tier_lists) {
        return;
    }

    phys_tier_list_t *t0 = &args->tier_lists->tiers[PHYS_TIER_0_DIRECT];
    phys_tier_list_t *t1 = &args->tier_lists->tiers[PHYS_TIER_1_NEAR];

    /* For each T0 body, find neighbors via swept AABB. */
    for (uint32_t i = 0; i < t0->count; ++i) {
        uint32_t body_idx = t0->indices[i];
        const phys_body_t *body = &args->bodies[body_idx];

        /* Step 1: Copy the body's AABB. */
        phys_aabb_t swept = args->aabbs[body_idx];

        /* Step 2: Extend by velocity * dt in the direction of motion. */
        vec3_t motion = vec3_scale(body->linear_vel, args->dt);

        /* Guard: if velocity contains NaN/Inf, skip this body to avoid
         * generating an invalid swept AABB. */
        if (isnan(motion.x) || isnan(motion.y) || isnan(motion.z) ||
            isinf(motion.x) || isinf(motion.y) || isinf(motion.z)) {
            continue;
        }
        if (motion.x > 0.0f) { swept.max.x += motion.x; }
        else                  { swept.min.x += motion.x; }
        if (motion.y > 0.0f) { swept.max.y += motion.y; }
        else                  { swept.min.y += motion.y; }
        if (motion.z > 0.0f) { swept.max.z += motion.z; }
        else                  { swept.min.z += motion.z; }

        /* Step 3: Expand by velocity_margin uniformly. */
        phys_aabb_expand(&swept, args->velocity_margin);

        /* Step 4: Query grid for neighbors. */
        uint32_t neighbors[HALO_MAX_QUERY_RESULTS];
        uint32_t count = phys_spatial_grid_query(
            args->grid, &swept, neighbors, HALO_MAX_QUERY_RESULTS);

        /* Step 5: Promote eligible neighbors to T1. */
        for (uint32_t j = 0; j < count; ++j) {
            uint32_t neighbor_idx = neighbors[j];

            /* Skip self. */
            if (neighbor_idx == body_idx) {
                continue;
            }

            /* Only valid indices. */
            if (neighbor_idx >= args->body_count) {
                continue;
            }

            const phys_body_t *neighbor = &args->bodies[neighbor_idx];

            /* Skip static bodies (inv_mass == 0). */
            if (neighbor->inv_mass <= 0.0f) {
                continue;
            }

            /* Skip bodies already in T0 (tier == 0). */
            if (neighbor->tier == PHYS_TIER_0_DIRECT) {
                continue;
            }

            phys_tier_list_add(t1, neighbor_idx);
        }
    }
}
