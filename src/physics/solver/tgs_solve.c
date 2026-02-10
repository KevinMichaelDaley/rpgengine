/**
 * @file tgs_solve.c
 * @brief TGS (Temporal Gauss-Seidel) velocity solver with split impulse.
 *
 * Two non-static functions:
 *   1. phys_tgs_init_velocities  — copy body velocities into workspace
 *   2. phys_stage_tgs_solve      — the main solver entry point
 *
 * Split impulse: after solving each normal row's velocity constraint,
 * a separate position-correction pseudo-impulse is solved into a
 * pseudo_velocities array.  The integrator adds pseudo_velocities to
 * position integration only — they are NOT written to body velocity.
 * This corrects penetration without injecting energy into the velocity
 * field, eliminating the upward drift that plagued the old separate
 * position projection + velocity sync pipeline.
 */

#include "ferrum/physics/tgs_solve.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/math/vec3.h"

/** Minimum penetration excess to correct (avoids micro-jitter). */
#define SPLIT_MIN_PHI 1e-6f

/* ── Internal: initialize velocity workspace from body state ──── */

/**
 * @brief Copy linear and angular velocities from bodies into the
 *        solver's velocity workspace array, and pre-apply gravity.
 *
 * Gravity is applied here (before the solver) so that the solver
 * can counteract gravitational acceleration in the same substep.
 * Without this, the integrator applies gravity after the solver,
 * causing bodies to slowly sink through resting contacts.
 *
 * @param args  Solver arguments.  Must not be NULL (caller checks).
 */
static void phys_tgs_init_velocities(const phys_tgs_solve_args_t *args)
{
    if (!args->bodies || !args->velocities) return;

    for (uint32_t i = 0; i < args->body_count; i++) {
        args->velocities[i].linear  = args->bodies[i].linear_vel;
        args->velocities[i].angular = args->bodies[i].angular_vel;

        /* Pre-apply gravity so the solver sees the full velocity
         * including gravitational acceleration.  Static/sleeping
         * bodies are skipped.  Use per-tier dt so bodies with fewer
         * substeps get the correct gravity increment. */
        if (args->bodies[i].inv_mass > 0.0f &&
            !phys_body_is_sleeping(&args->bodies[i])) {
            float body_dt = args->dt;
            if (args->tier_substep_counts && args->tick_dt > 0.0f) {
                uint8_t tier = args->bodies[i].tier;
                uint32_t ts = args->tier_substep_counts[tier];
                if (ts == 0) { ts = 1; }
                body_dt = args->tick_dt / (float)ts;
            }
            args->velocities[i].linear = vec3_add(
                args->velocities[i].linear,
                vec3_scale(args->gravity, body_dt));
        }
    }
}

/* ── Solve a single Jacobian row (static helper) ────────────── */

/**
 * @brief Solve one constraint row: compute impulse delta, clamp
 *        the accumulated lambda, and apply velocity corrections.
 */
static void solve_row(phys_jacobian_row_t *row,
                       phys_velocity_t *va,
                       phys_velocity_t *vb,
                       float inv_mass_a,
                       const phys_vec3_t *inv_i_a,
                       float inv_mass_b,
                       const phys_vec3_t *inv_i_b)
{
    /* Compute J·v (relative velocity along the constraint direction). */
    float jv = vec3_dot(row->J_va, va->linear)
             + vec3_dot(row->J_wa, va->angular)
             + vec3_dot(row->J_vb, vb->linear)
             + vec3_dot(row->J_wb, vb->angular);

    /* Impulse delta from constraint violation. */
    float delta_lambda = (row->bias - jv) * row->effective_mass;

    /* Clamp accumulated impulse within bounds. */
    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
    if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
    delta_lambda = row->lambda - old_lambda;

    /* Apply linear velocity corrections. */
    va->linear = vec3_add(va->linear,
                          vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    vb->linear = vec3_add(vb->linear,
                          vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    /* Apply angular velocity corrections (diagonal inertia). */
    va->angular.x += inv_i_a->x * row->J_wa.x * delta_lambda;
    va->angular.y += inv_i_a->y * row->J_wa.y * delta_lambda;
    va->angular.z += inv_i_a->z * row->J_wa.z * delta_lambda;

    vb->angular.x += inv_i_b->x * row->J_wb.x * delta_lambda;
    vb->angular.y += inv_i_b->y * row->J_wb.y * delta_lambda;
    vb->angular.z += inv_i_b->z * row->J_wb.z * delta_lambda;
}

/* ── Solve split-impulse position correction row (static helper) ─ */

/**
 * @brief Solve the position correction pseudo-impulse for a normal row.
 *
 * Computes the penetration bias (Φ - slop) / dt, then solves a PGS
 * row against the pseudo_velocities workspace (not the real velocities).
 * The accumulated pseudo_lambda is clamped ≥ 0 (contacts only push apart).
 *
 * @param row           Normal Jacobian row (pseudo_lambda is updated).
 * @param pva           Pseudo-velocity for body A.
 * @param pvb           Pseudo-velocity for body B.
 * @param penetration   Raw penetration depth from contact.
 * @param slop          Penetration slop threshold.
 * @param inv_dt        1 / dt.
 * @param inv_mass_a    Inverse mass of body A.
 * @param inv_i_a       Diagonal inverse inertia of body A.
 * @param inv_mass_b    Inverse mass of body B.
 * @param inv_i_b       Diagonal inverse inertia of body B.
 */
static void solve_position_row(phys_jacobian_row_t *row,
                                phys_velocity_t *pva,
                                phys_velocity_t *pvb,
                                float penetration,
                                float slop,
                                float inv_dt,
                                float inv_mass_a,
                                const phys_vec3_t *inv_i_a,
                                float inv_mass_b,
                                const phys_vec3_t *inv_i_b)
{
    float excess = penetration - slop;
    if (excess < SPLIT_MIN_PHI) { return; }

    /* Position correction bias: target pseudo-velocity to resolve
     * the penetration excess within one substep. */
    float pos_bias = excess * inv_dt;

    /* Current pseudo-velocity along constraint normal. */
    float jv = vec3_dot(row->J_va, pva->linear)
             + vec3_dot(row->J_wa, pva->angular)
             + vec3_dot(row->J_vb, pvb->linear)
             + vec3_dot(row->J_wb, pvb->angular);

    float delta_lambda = (pos_bias - jv) * row->effective_mass;

    /* Clamp accumulated pseudo-lambda ≥ 0 (contacts only separate). */
    float old_lambda = row->pseudo_lambda;
    float new_lambda = old_lambda + delta_lambda;
    if (new_lambda < 0.0f) { new_lambda = 0.0f; }
    delta_lambda = new_lambda - old_lambda;
    row->pseudo_lambda = new_lambda;

    if (fabsf(delta_lambda) < 1e-10f) { return; }

    /* Apply pseudo-velocity corrections. */
    pva->linear = vec3_add(pva->linear,
                           vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    pvb->linear = vec3_add(pvb->linear,
                           vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    pva->angular.x += inv_i_a->x * row->J_wa.x * delta_lambda;
    pva->angular.y += inv_i_a->y * row->J_wa.y * delta_lambda;
    pva->angular.z += inv_i_a->z * row->J_wa.z * delta_lambda;

    pvb->angular.x += inv_i_b->x * row->J_wb.x * delta_lambda;
    pvb->angular.y += inv_i_b->y * row->J_wb.y * delta_lambda;
    pvb->angular.z += inv_i_b->z * row->J_wb.z * delta_lambda;
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args)
{
    if (!args || !args->islands) return;

    /* Copy body velocities into solver workspace. */
    phys_tgs_init_velocities(args);

    /* Zero pseudo-velocities if split impulse is active. */
    phys_velocity_t *pseudo = args->pseudo_velocities;
    if (pseudo) {
        for (uint32_t i = 0; i < args->body_count; i++) {
            pseudo[i] = (phys_velocity_t){{0,0,0},{0,0,0}};
        }
    }

    const float inv_dt = (args->dt > 0.0f) ? (1.0f / args->dt) : 0.0f;
    const float slop = args->slop;

    const phys_island_list_t *islands = args->islands;

    /* Process each island independently. */
    for (uint32_t i = 0; i < islands->count; i++) {
        const phys_island_t *island = &islands->islands[i];
        if (island->sleeping || island->skip) continue;

        /* Skip XPBD islands — they are handled by Stage 11b. */
        if (island->constraint_count > 0) {
            uint32_t first_ci = island->constraint_indices[0];
            if (args->constraints[first_ci].solver_mode == PHYS_SOLVER_XPBD) {
                continue;
            }
        }

        /* Iterate the sequential impulse solver. */
        for (uint32_t iter = 0; iter < args->iterations; iter++) {
            for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
                uint32_t c_idx = island->constraint_indices[ci];
                phys_constraint_t *c = &args->constraints[c_idx];

                phys_velocity_t *va = &args->velocities[c->body_a];
                phys_velocity_t *vb = &args->velocities[c->body_b];

                float inv_mass_a = args->bodies[c->body_a].inv_mass;
                float inv_mass_b = args->bodies[c->body_b].inv_mass;
                const phys_vec3_t *inv_i_a =
                    &args->bodies[c->body_a].inv_inertia_diag;
                const phys_vec3_t *inv_i_b =
                    &args->bodies[c->body_b].inv_inertia_diag;

                /* Solve normal row first (row 0). */
                solve_row(&c->rows[0], va, vb,
                          inv_mass_a, inv_i_a,
                          inv_mass_b, inv_i_b);

                /* Split impulse: solve position correction into
                 * pseudo-velocities using the same constraint normal. */
                if (pseudo) {
                    solve_position_row(
                        &c->rows[0],
                        &pseudo[c->body_a], &pseudo[c->body_b],
                        c->penetration, slop, inv_dt,
                        inv_mass_a, inv_i_a,
                        inv_mass_b, inv_i_b);
                }

                /* Coulomb friction cone: clamp tangent impulses to
                 * ±friction * accumulated_normal_impulse. */
                float friction_limit = c->friction * c->rows[0].lambda;
                for (uint8_t r = 1; r < c->row_count; r++) {
                    c->rows[r].lambda_min = -friction_limit;
                    c->rows[r].lambda_max =  friction_limit;
                    solve_row(&c->rows[r], va, vb,
                              inv_mass_a, inv_i_a,
                              inv_mass_b, inv_i_b);
                }
            }
        }
    }
}
