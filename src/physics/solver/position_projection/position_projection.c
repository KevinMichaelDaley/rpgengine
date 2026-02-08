/**
 * @file position_projection.c
 * @brief Per-island position projection using sparse iterative Gauss-Seidel.
 *
 * Uses the full block-diagonal Jacobian (linear J_v + angular J_w) so
 * that rotational constraint violations are corrected alongside
 * translational ones.  Complexity is O(nc × iterations).
 *
 * Non-static functions (1):
 *   1. phys_position_projection — main entry point
 *
 * (phys_dense_ldlt_solve remains in ldlt_solve.c for unit test coverage)
 */

#include "ferrum/physics/position_projection.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tgs_solve.h"    /* phys_velocity_t */
#include "ferrum/math/vec3.h"

/** Minimum penetration to correct (in addition to slop). */
#define MIN_CORRECTION 1e-6f

/** Number of Gauss-Seidel iterations for position correction. */
#define PP_GS_ITERATIONS 8

/**
 * @brief Extract the penetration depth from a constraint for position
 *        projection, applying slop threshold.
 */
static float compute_phi(const phys_constraint_t *c,
                         float slop)
{
    float penetration = c->penetration;
    float excess = penetration - slop;
    if (excess < MIN_CORRECTION) {
        return 0.0f;
    }
    return excess;
}

/**
 * @brief Compute the current position-space constraint error for a
 *        single normal constraint, accounting for both linear and
 *        angular corrections already accumulated in deltas.
 *
 * Returns the residual penetration after applying current deltas:
 *   residual = phi - (J_va · delta_lin_a + J_wa · delta_ang_a
 *                    + J_vb · delta_lin_b + J_wb · delta_ang_b)
 *
 * A positive residual means the constraint is still violated.
 */
static float compute_residual(const phys_constraint_t *c,
                              const phys_velocity_t *deltas,
                              float phi)
{
    const phys_jacobian_row_t *row = &c->rows[0];
    uint32_t a = c->body_a;
    uint32_t b = c->body_b;

    /* Full Jacobian dot product: J · delta_q (linear + angular). */
    float j_delta = vec3_dot(row->J_va, deltas[a].linear)
                  + vec3_dot(row->J_wa, deltas[a].angular)
                  + vec3_dot(row->J_vb, deltas[b].linear)
                  + vec3_dot(row->J_wb, deltas[b].angular);

    return phi - j_delta;
}

/**
 * @brief Apply a generalized position correction impulse to both bodies
 *        of a constraint:
 *          delta_lin += M^-1  * J_v^T * delta_lambda
 *          delta_ang += I^-1  * J_w^T * delta_lambda
 */
static void apply_position_impulse(
    phys_velocity_t *deltas,
    const phys_constraint_t *c,
    const phys_body_t *bodies,
    float delta_lambda)
{
    const phys_jacobian_row_t *row = &c->rows[0];
    uint32_t a = c->body_a;
    uint32_t b = c->body_b;

    /* Body A: linear correction. */
    float inv_ma = bodies[a].inv_mass;
    deltas[a].linear = vec3_add(deltas[a].linear,
                                vec3_scale(row->J_va, inv_ma * delta_lambda));

    /* Body A: angular correction (diagonal inverse inertia). */
    const phys_vec3_t *inv_ia = &bodies[a].inv_inertia_diag;
    deltas[a].angular.x += inv_ia->x * row->J_wa.x * delta_lambda;
    deltas[a].angular.y += inv_ia->y * row->J_wa.y * delta_lambda;
    deltas[a].angular.z += inv_ia->z * row->J_wa.z * delta_lambda;

    /* Body B: linear correction. */
    float inv_mb = bodies[b].inv_mass;
    deltas[b].linear = vec3_add(deltas[b].linear,
                                vec3_scale(row->J_vb, inv_mb * delta_lambda));

    /* Body B: angular correction (diagonal inverse inertia). */
    const phys_vec3_t *inv_ib = &bodies[b].inv_inertia_diag;
    deltas[b].angular.x += inv_ib->x * row->J_wb.x * delta_lambda;
    deltas[b].angular.y += inv_ib->y * row->J_wb.y * delta_lambda;
    deltas[b].angular.z += inv_ib->z * row->J_wb.z * delta_lambda;
}

void phys_position_projection(const phys_position_projection_args_t *args)
{
    if (!args) { return; }

    phys_position_projection_result_t *result = args->result;
    if (!result) { return; }

    result->success = false;
    result->correction_deltas = NULL;

    const struct phys_island *island = args->island;
    if (!island || !args->constraints || !args->bodies ||
        !args->arena || args->body_count == 0) {
        return;
    }

    const uint32_t body_count = args->body_count;
    const float slop = args->slop;

    /* Use caller-provided shared array if available, otherwise allocate. */
    phys_velocity_t *deltas = args->shared_deltas;

    if (!deltas) {
        deltas = phys_frame_arena_alloc(
            args->arena, body_count * sizeof(phys_velocity_t),
            _Alignof(phys_velocity_t));
    }

    if (!deltas) { return; }

    /* Zero only the island's body entries, not the entire array. */
    for (uint32_t bi = 0; bi < island->body_count; bi++) {
        uint32_t idx = island->body_indices[bi];
        deltas[idx] = (phys_velocity_t){{0.0f, 0.0f, 0.0f},
                                         {0.0f, 0.0f, 0.0f}};
    }

    /* Also zero deltas for static bodies referenced by constraints.
     * Static bodies aren't in body_indices (only dynamic bodies are),
     * but their deltas are read by velocity_sync for target velocity
     * computation.  Without this, static body deltas contain
     * uninitialized arena garbage, causing NaN propagation. */
    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t con_idx = island->constraint_indices[ci];
        const phys_constraint_t *c = &args->constraints[con_idx];
        uint32_t a = c->body_a, b = c->body_b;
        if (a < body_count && args->bodies[a].inv_mass == 0.0f) {
            deltas[a] = (phys_velocity_t){{0.0f, 0.0f, 0.0f},
                                           {0.0f, 0.0f, 0.0f}};
        }
        if (b < body_count && args->bodies[b].inv_mass == 0.0f) {
            deltas[b] = (phys_velocity_t){{0.0f, 0.0f, 0.0f},
                                           {0.0f, 0.0f, 0.0f}};
        }
    }

    result->correction_deltas = deltas;
    result->success = true;

    /* Sleeping islands produce zero corrections. */
    if (island->sleeping || island->skip) { return; }

    const uint32_t nc = island->constraint_count;
    if (nc == 0) { return; }

    /* ── Step 1: Compute Phi vector (penetration excess above slop) ─ */
    float *phi = phys_frame_arena_alloc(
        args->arena, nc * sizeof(float), _Alignof(float));
    float *lambda = phys_frame_arena_alloc(
        args->arena, nc * sizeof(float), _Alignof(float));
    if (!phi || !lambda) { return; }

    uint32_t active_count = 0;
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t ci = island->constraint_indices[i];
        phi[i] = compute_phi(&args->constraints[ci], slop);
        lambda[i] = 0.0f;
        if (phi[i] > MIN_CORRECTION) { active_count++; }
    }

    /* If no active penetrations, we're done (zero corrections). */
    if (active_count == 0) { return; }

    /* ── Step 2: Gauss-Seidel iterations ───────────────────────────
     * For each constraint, compute the residual position error using
     * the full Jacobian (linear + angular) and apply a diagonal
     * correction using the pre-computed effective_mass.  The
     * effective_mass already accounts for both linear and angular
     * contributions (J M^-1 J^T includes I^-1 terms). */
    for (uint32_t iter = 0; iter < PP_GS_ITERATIONS; iter++) {
        for (uint32_t i = 0; i < nc; i++) {
            if (phi[i] <= MIN_CORRECTION) { continue; }

            uint32_t ci = island->constraint_indices[i];
            const phys_constraint_t *con = &args->constraints[ci];

            /* Compute residual = phi - J * current_delta (full Jacobian). */
            float residual = compute_residual(con, deltas, phi[i]);

            /* No correction needed if residual is non-positive. */
            if (residual <= MIN_CORRECTION) { continue; }

            float eff_mass = con->rows[0].effective_mass;
            if (eff_mass <= 0.0f) { continue; }

            float delta_lambda = eff_mass * residual;

            /* Clamp accumulated lambda to be non-negative (contacts push apart). */
            float new_lambda = lambda[i] + delta_lambda;
            if (new_lambda < 0.0f) { new_lambda = 0.0f; }
            delta_lambda = new_lambda - lambda[i];
            lambda[i] = new_lambda;

            if (fabsf(delta_lambda) < 1e-10f) { continue; }

            /* Apply generalized position impulse (linear + angular). */
            apply_position_impulse(deltas, con, args->bodies,
                                   delta_lambda);
        }
    }
}
