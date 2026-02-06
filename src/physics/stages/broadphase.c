/**
 * @file broadphase.c
 * @brief Broadphase collision detection stage implementation.
 *
 * Iterates active tier bodies (T0–T4), queries the spatial grid for
 * AABB overlap candidates, performs precise overlap tests, excludes
 * static-static pairs, and outputs canonical (body_a < body_b) pairs.
 */

#include "ferrum/physics/broadphase.h"

#include <stddef.h>

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/tier_list.h"

/** Maximum candidates returned by a single grid query. */
#define BROADPHASE_MAX_CANDIDATES 256

void phys_stage_broadphase(const phys_broadphase_args_t *args) {
    if (!args) {
        return;
    }
    if (!args->bodies || !args->aabbs || !args->grid ||
        !args->tier_lists || !args->pairs_out || !args->pair_count_out) {
        if (args->pair_count_out) {
            *args->pair_count_out = 0;
        }
        return;
    }

    uint32_t pair_count = 0;

    /* Iterate active tiers T0 through T4. */
    for (int tier = PHYS_TIER_0_DIRECT; tier <= PHYS_TIER_4_BACKGROUND; ++tier) {
        const phys_tier_list_t *list = &args->tier_lists->tiers[tier];

        for (uint32_t i = 0; i < list->count; ++i) {
            uint32_t body_a = list->indices[i];
            const phys_aabb_t *aabb_a = &args->aabbs[body_a];

            /* Query spatial grid for candidate overlaps. */
            uint32_t candidates[BROADPHASE_MAX_CANDIDATES];
            uint32_t cand_count = phys_spatial_grid_query(
                args->grid, aabb_a, candidates, BROADPHASE_MAX_CANDIDATES);

            for (uint32_t j = 0; j < cand_count; ++j) {
                uint32_t body_b = candidates[j];

                /* Skip self-pairs and enforce canonical order. */
                if (body_a >= body_b) {
                    continue;
                }

                /* Skip static-static pairs (both inv_mass == 0, non-kinematic). */
                if (phys_body_is_static(&args->bodies[body_a]) &&
                    phys_body_is_static(&args->bodies[body_b])) {
                    continue;
                }

                /* Precise AABB overlap test. */
                if (!phys_aabb_overlap(aabb_a, &args->aabbs[body_b])) {
                    continue;
                }

                /* Emit pair if buffer has room. */
                if (pair_count < args->max_pairs) {
                    args->pairs_out[pair_count].body_a = body_a;
                    args->pairs_out[pair_count].body_b = body_b;
                    pair_count++;
                }
            }
        }
    }

    *args->pair_count_out = pair_count;
}
