/**
 * @file constraint_build_stage.c
 * @brief Stage 9: Constraint Build — generates Jacobian constraints
 *        from contact manifolds with stabilization hints applied.
 */

#include "ferrum/physics/constraint_stage.h"

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/stabilization.h"

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

            /* Load warmstart impulses from manifold cache. */
            c->rows[0].lambda = manifold->normal_impulse[p];
            c->rows[1].lambda = manifold->tangent_impulse[p][0];
            c->rows[2].lambda = manifold->tangent_impulse[p][1];
        }
    }

    *args->constraint_count_out = c_count;
}
