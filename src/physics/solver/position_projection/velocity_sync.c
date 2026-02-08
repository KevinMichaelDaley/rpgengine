/**
 * @file velocity_sync.c
 * @brief Per-island sparse Gauss-Seidel velocity synchronization.
 *
 * After position projection corrects penetration (linear + angular),
 * body velocities must reflect the corrected geometry.  We build a
 * sparse velocity-level system using the full block-diagonal Jacobian
 * (J_v for linear, J_w for angular) and solve via Gauss-Seidel:
 *
 *   For each constraint c with normal row:
 *     target  = J · (delta_q / dt) * erp
 *             = (J_va·dlin_a + J_wa·dang_a + J_vb·dlin_b + J_wb·dang_b) / dt * erp
 *     current = J · v
 *             = J_va·va_lin + J_wa·va_ang + J_vb·vb_lin + J_wb·vb_ang
 *     residual = target - current
 *     delta_lambda = effective_mass * residual
 *     Apply linear:  v_lin += M^-1  * J_v^T * delta_lambda
 *     Apply angular: v_ang += I^-1  * J_w^T * delta_lambda
 *
 * This naturally handles bodies touched by multiple constraints because
 * GS propagates corrections through shared bodies across iterations.
 *
 * The solver is fully bilateral — lambda may be positive or negative —
 * so it finds the true least-squares velocity field matching the
 * position corrections without directional bias.  A unilateral clamp
 * (lambda ≥ 0) was previously used but caused upward velocity bias
 * that manifested as stack hovering on clients replaying server
 * velocities without a local solver.
 *
 * See ref/sparse_stabilization.tex Section 5b.
 *
 * Non-static functions (1):
 *   1. phys_velocity_sync_normals
 */

#include "ferrum/physics/velocity_sync.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/tgs_solve.h"    /* phys_velocity_t */
#include "ferrum/math/vec3.h"

/** Number of Gauss-Seidel iterations for velocity sync. */
#define VS_GS_ITERATIONS 4

void phys_velocity_sync_normals(
    const phys_velocity_sync_args_t *args)
{
    if (!args || !args->island || !args->constraints ||
        !args->bodies || !args->correction_deltas) {
        return;
    }

    const phys_island_t *island = args->island;
    const float dt = args->dt;
    if (dt <= 0.0f) { return; }
    if (island->sleeping || island->skip) { return; }

    const uint32_t nc = island->constraint_count;
    if (nc == 0) { return; }

    const float inv_dt = 1.0f / dt;

    /* Velocity sync fraction (ERP-like): controls how aggressively
     * velocity is adjusted to match position corrections.  A value of
     * 1.0 converts the full position correction into velocity, which
     * injects too much energy and causes rubber-ball bouncing.  0.2
     * gently nudges velocities so bodies separate over several
     * substeps without creating visible bounce. */
    const float vel_sync_erp = 0.2f;

    /* Gauss-Seidel iterations over the island's constraint system. */
    for (uint32_t iter = 0; iter < VS_GS_ITERATIONS; iter++) {
        for (uint32_t ci = 0; ci < nc; ci++) {
            uint32_t con_idx = island->constraint_indices[ci];
            const phys_constraint_t *c = &args->constraints[con_idx];

            const phys_jacobian_row_t *row = &c->rows[0];
            uint32_t idx_a = c->body_a;
            uint32_t idx_b = c->body_b;
            phys_body_t *body_a = &args->bodies[idx_a];
            phys_body_t *body_b = &args->bodies[idx_b];
            const phys_velocity_t *da = &args->correction_deltas[idx_a];
            const phys_velocity_t *db = &args->correction_deltas[idx_b];

            /* Target relative velocity from generalized correction deltas
             * (full Jacobian: linear + angular), scaled by ERP. */
            float target_vn =
                (vec3_dot(row->J_va, vec3_scale(da->linear,  inv_dt))
               + vec3_dot(row->J_wa, vec3_scale(da->angular, inv_dt))
               + vec3_dot(row->J_vb, vec3_scale(db->linear,  inv_dt))
               + vec3_dot(row->J_wb, vec3_scale(db->angular, inv_dt)))
                * vel_sync_erp;

            if (fabsf(target_vn) < 1e-8f) { continue; }

            /* Current relative velocity: full J · v (linear + angular). */
            float current_vn =
                vec3_dot(row->J_va, body_a->linear_vel)
              + vec3_dot(row->J_wa, body_a->angular_vel)
              + vec3_dot(row->J_vb, body_b->linear_vel)
              + vec3_dot(row->J_wb, body_b->angular_vel);

            float residual = target_vn - current_vn;

            float eff_mass = row->effective_mass;
            if (eff_mass <= 0.0f) { continue; }

            float delta_lambda = eff_mass * residual;
            if (fabsf(delta_lambda) < 1e-10f) { continue; }

            /* Apply linear velocity impulse: v_lin += M^-1 * J_v^T * dλ. */
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

            /* Apply angular velocity impulse: v_ang += I^-1 * J_w^T * dλ. */
            const phys_vec3_t *inv_ia = &body_a->inv_inertia_diag;
            if (inv_ia->x > 0.0f || inv_ia->y > 0.0f || inv_ia->z > 0.0f) {
                body_a->angular_vel.x += inv_ia->x * row->J_wa.x * delta_lambda;
                body_a->angular_vel.y += inv_ia->y * row->J_wa.y * delta_lambda;
                body_a->angular_vel.z += inv_ia->z * row->J_wa.z * delta_lambda;
            }
            const phys_vec3_t *inv_ib = &body_b->inv_inertia_diag;
            if (inv_ib->x > 0.0f || inv_ib->y > 0.0f || inv_ib->z > 0.0f) {
                body_b->angular_vel.x += inv_ib->x * row->J_wb.x * delta_lambda;
                body_b->angular_vel.y += inv_ib->y * row->J_wb.y * delta_lambda;
                body_b->angular_vel.z += inv_ib->z * row->J_wb.z * delta_lambda;
            }
        }
    }
}
