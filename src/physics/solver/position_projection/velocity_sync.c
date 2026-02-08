/**
 * @file velocity_sync.c
 * @brief Per-island sparse Gauss-Seidel velocity synchronization.
 *
 * After position projection corrects penetration, body velocities must
 * reflect the corrected geometry.  We build a sparse velocity-level
 * system over all island constraints and solve via Gauss-Seidel:
 *
 *   For each constraint c with normal n:
 *     target_vn = dot(delta_q_b / dt, n) - dot(delta_q_a / dt, n)
 *     current_vn = J · v  (relative normal velocity from integrated state)
 *     residual = target_vn - current_vn
 *     delta_lambda = effective_mass * residual
 *     v_a -= inv_mass_a * n * delta_lambda
 *     v_b += inv_mass_b * n * delta_lambda
 *
 * This naturally handles bodies touched by multiple constraints because
 * GS propagates corrections through shared bodies across iterations.
 *
 * See ref/sparse_stabilization.tex Section 5b.
 *
 * Non-static functions (1):
 *   1. phys_velocity_sync_normals
 */

#include "ferrum/physics/velocity_sync.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/math/vec3.h"

/** Number of Gauss-Seidel iterations for velocity sync. */
#define VS_GS_ITERATIONS 4

void phys_velocity_sync_normals(
    const phys_velocity_sync_args_t *args)
{
    if (!args || !args->island || !args->constraints ||
        !args->bodies || !args->position_deltas) {
        return;
    }

    const phys_island_t *island = args->island;
    const float dt = args->dt;
    if (dt <= 0.0f) { return; }
    if (island->sleeping || island->skip) { return; }

    const uint32_t nc = island->constraint_count;
    if (nc == 0) { return; }

    const float inv_dt = 1.0f / dt;

    /* Gauss-Seidel iterations over the island's constraint system.
     * For each constraint, compute the target relative normal velocity
     * (from position deltas) and adjust body velocities to match. */
    for (uint32_t iter = 0; iter < VS_GS_ITERATIONS; iter++) {
        for (uint32_t ci = 0; ci < nc; ci++) {
            uint32_t con_idx = island->constraint_indices[ci];
            const phys_constraint_t *c = &args->constraints[con_idx];

            const phys_jacobian_row_t *row = &c->rows[0];
            uint32_t idx_a = c->body_a;
            uint32_t idx_b = c->body_b;
            phys_body_t *body_a = &args->bodies[idx_a];
            phys_body_t *body_b = &args->bodies[idx_b];

            /* Target relative normal velocity from position correction:
             * target_vn = J · (delta_q / dt)
             *           = -n · (delta_q_a / dt) + n · (delta_q_b / dt). */
            float target_vn =
                vec3_dot(row->J_va,
                         vec3_scale(args->position_deltas[idx_a], inv_dt))
              + vec3_dot(row->J_vb,
                         vec3_scale(args->position_deltas[idx_b], inv_dt));

            /* If the position correction produced no target velocity along
             * this constraint normal, skip it — no velocity sync needed. */
            if (fabsf(target_vn) < 1e-8f) { continue; }

            /* Current relative normal velocity: J · v.
             * J_va = -n, J_vb = +n, so J·v = -n·va + n·vb = n·(vb - va). */
            float current_vn = vec3_dot(row->J_va, body_a->linear_vel)
                             + vec3_dot(row->J_vb, body_b->linear_vel);

            float residual = target_vn - current_vn;

            /* Only apply correction if it would push bodies apart
             * (positive lambda = separation impulse). */
            float eff_mass = row->effective_mass;
            if (eff_mass <= 0.0f) { continue; }

            float delta_lambda = eff_mass * residual;

            /* Clamp: velocity sync should only push apart, not pull together. */
            if (delta_lambda < 0.0f) { continue; }

            if (fabsf(delta_lambda) < 1e-10f) { continue; }

            /* Apply velocity impulse: v += M^-1 J^T delta_lambda. */
            if (body_a->inv_mass > 0.0f) {
                body_a->linear_vel = vec3_add(
                    body_a->linear_vel,
                    vec3_scale(row->J_va, body_a->inv_mass * delta_lambda));
            }
            if (body_b->inv_mass > 0.0f) {
                body_b->linear_vel = vec3_add(
                    body_b->linear_vel,
                    vec3_scale(row->J_vb, body_b->inv_mass * delta_lambda));
            }
        }
    }
}
