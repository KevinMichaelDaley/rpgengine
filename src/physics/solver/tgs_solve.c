/**
 * @file tgs_solve.c
 * @brief TGS (Temporal Gauss-Seidel) velocity solver implementation.
 *
 * Two non-static functions:
 *   1. phys_tgs_init_velocities  — copy body velocities into workspace
 *   2. phys_stage_tgs_solve      — the main solver entry point
 */

#include "ferrum/physics/tgs_solve.h"

#include <stddef.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/math/vec3.h"

/* ── Internal: initialize velocity workspace from body state ──── */

/**
 * @brief Copy linear and angular velocities from bodies into the
 *        solver's velocity workspace array.
 *
 * @param args  Solver arguments.  Must not be NULL (caller checks).
 */
static void phys_tgs_init_velocities(const phys_tgs_solve_args_t *args)
{
    if (!args->bodies || !args->velocities) return;

    for (uint32_t i = 0; i < args->body_count; i++) {
        args->velocities[i].linear  = args->bodies[i].linear_vel;
        args->velocities[i].angular = args->bodies[i].angular_vel;
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

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_tgs_solve(const phys_tgs_solve_args_t *args)
{
    if (!args || !args->islands) return;

    /* Copy body velocities into solver workspace. */
    phys_tgs_init_velocities(args);

    const phys_island_list_t *islands = args->islands;

    /* Process each island independently. */
    for (uint32_t i = 0; i < islands->count; i++) {
        const phys_island_t *island = &islands->islands[i];
        if (island->sleeping || island->skip) continue;

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
