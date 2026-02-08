/**
 * @file constraint_build_stage.c
 * @brief Stage 9: Constraint Build — generates Jacobian constraints
 *        from contact manifolds with stabilization hints applied.
 */

#include "ferrum/physics/constraint_stage.h"

#include <math.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/step_plan.h"

void phys_stage_constraint_build(const phys_constraint_build_args_t *args)
{
    if (!args) { return; }
    if (!args->manifolds || !args->hints || !args->bodies ||
        !args->constraints_out || !args->constraint_count_out) {
        return;
    }

    uint32_t c_count = 0;

    for (uint32_t m = 0; m < args->manifold_count; ++m) {
        const phys_manifold_t *manifold = &args->manifolds[m];
        const phys_stab_hint_t *hint    = &args->hints[m];
        const phys_body_t *body_a       = &args->bodies[manifold->body_a];
        const phys_body_t *body_b       = &args->bodies[manifold->body_b];

        /* Apply stabilization hints to material properties. */
        float friction    = manifold->friction    * hint->friction_scale;
        float restitution = manifold->restitution * hint->restitution_scale;

        for (uint8_t p = 0; p < manifold->point_count; ++p) {
            if (c_count >= args->max_constraints) { break; }

            phys_constraint_t *c = &args->constraints_out[c_count++];

            /* Build Jacobian rows (normal + 2 friction tangent). */
            phys_constraint_build_contact(c, body_a, body_b,
                                          &manifold->points[p],
                                          friction, restitution,
                                          args->dt, args->baumgarte,
                                          args->slop);

            /* Back-references for solver writeback. */
            c->body_a       = manifold->body_a;
            c->body_b       = manifold->body_b;
            c->manifold_idx = m;
            c->point_idx    = (uint8_t)p;

            /* Determine solver mode from body tiers. */
            c->solver_mode = (uint8_t)phys_tier_cross_solver_mode(
                (phys_tier_t)body_a->tier, (phys_tier_t)body_b->tier);

            /* For TGS-tier constraints, remove Baumgarte bias from the
             * normal row.  Position projection handles penetration
             * correction instead, which avoids energy injection. */
            if (c->solver_mode == 0 && args->dt > 0.0f) {
                float pen_excess = manifold->points[p].penetration
                                 - args->slop;
                if (pen_excess < 0.0f) { pen_excess = 0.0f; }
                float baumgarte_bias = (args->baumgarte / args->dt)
                                     * pen_excess;
                c->rows[0].bias -= baumgarte_bias;
            }

            /* Load warmstart impulses from manifold cache.
             * Sanitize NaN/Inf to prevent solver corruption. */
            c->rows[0].lambda = manifold->normal_impulse[p];
            c->rows[1].lambda = manifold->tangent_impulse[p][0];
            c->rows[2].lambda = manifold->tangent_impulse[p][1];
            for (uint8_t r = 0; r < 3; ++r) {
                if (isnan(c->rows[r].lambda) || isinf(c->rows[r].lambda)) {
                    c->rows[r].lambda = 0.0f;
                }
            }
        }
    }

    *args->constraint_count_out = c_count;
}
