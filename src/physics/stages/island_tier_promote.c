/**
 * @file island_tier_promote.c
 * @brief Stage 10b: Island Tier Promotion.
 *
 * For each non-sleeping island, finds the minimum tier among dynamic
 * bodies and promotes all dynamic bodies to that tier.  Updates
 * constraint solver_mode fields to match.
 *
 * 1 non-static function: phys_stage_island_tier_promote
 */

#include "ferrum/physics/island_tier_promote.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"

void phys_stage_island_tier_promote(
    const phys_island_tier_promote_args_t *args)
{
    if (!args || !args->islands || !args->bodies) {
        return;
    }

    const phys_island_list_t *islands = args->islands;

    for (uint32_t i = 0; i < islands->count; ++i) {
        const phys_island_t *isle = &islands->islands[i];
        if (isle->sleeping || isle->skip) { continue; }
        if (isle->body_count <= 1) { continue; }

        /* Pass 1: find minimum tier among dynamic bodies.
         * Ghost bodies (NO_BROADPHASE) contribute their tier to the
         * scan so ghost-only islands get the correct solver mode,
         * but they are not overwritten in Pass 2. */
        uint8_t min_tier = PHYS_TIER_5_SLEEPING;
        for (uint32_t b = 0; b < isle->body_count; ++b) {
            uint32_t idx = isle->body_indices[b];
            if (idx >= args->body_count) { continue; }
            const phys_body_t *body = &args->bodies[idx];

            /* Skip static/kinematic bodies. */
            if (body->inv_mass <= 0.0f) { continue; }

            if (body->tier < min_tier) {
                min_tier = body->tier;
            }
        }

        /* No dynamic bodies found — nothing to promote. */
        if (min_tier == PHYS_TIER_5_SLEEPING) { continue; }

        /* Pass 2: promote all dynamic bodies to min_tier. */
        for (uint32_t b = 0; b < isle->body_count; ++b) {
            uint32_t idx = isle->body_indices[b];
            if (idx >= args->body_count) { continue; }
            phys_body_t *body = &args->bodies[idx];

            if (body->inv_mass <= 0.0f) { continue; }
            body->tier = min_tier;
        }

        /* Pass 3: update constraint solver_mode to match promoted tier. */
        if (!args->constraints) { continue; }
        phys_solver_mode_t mode = phys_tier_cross_solver_mode(
            (phys_tier_t)min_tier, (phys_tier_t)min_tier);

        for (uint32_t c = 0; c < isle->constraint_count; ++c) {
            uint32_t ci = isle->constraint_indices[c];
            if (ci >= args->constraint_count) { continue; }
            phys_constraint_t *con = &args->constraints[ci];
            con->solver_mode = (uint8_t)mode;
        }
    }
}
