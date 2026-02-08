/**
 * @file position_projection.c
 * @brief Per-island position projection using sparse iterative Gauss-Seidel.
 *
 * Replaces the original dense A = J M^-1 J^T + LDLT factorization with
 * a sparse Gauss-Seidel iteration that uses the pre-computed per-constraint
 * effective_mass (diagonal of A).  Complexity is O(nc × iterations) instead
 * of O(nc³).
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
    /* Use the raw penetration stored on the constraint. */
    float penetration = c->penetration;

    /* Apply slop threshold. */
    float excess = penetration - slop;
    if (excess < MIN_CORRECTION) {
        return 0.0f;
    }
    return excess;
}

/**
 * @brief Compute the current position-space constraint error for a
 *        single normal constraint, accounting for corrections already
 *        accumulated in pos_deltas.
 *
 * Returns the residual penetration after applying current deltas:
 *   residual = phi - J * delta_pos
 *
 * A positive residual means the constraint is still violated.
 */
static float compute_residual(const phys_constraint_t *c,
                              const phys_vec3_t *pos_deltas,
                              float phi)
{
    const phys_jacobian_row_t *row = &c->rows[0];
    uint32_t a = c->body_a;
    uint32_t b = c->body_b;

    /* J * delta_pos = J_va · delta_a + J_vb · delta_b
     * (ignoring angular for position projection). */
    float j_delta = vec3_dot(row->J_va, pos_deltas[a])
                  + vec3_dot(row->J_vb, pos_deltas[b]);

    return phi - j_delta;
}

/**
 * @brief Apply a position correction impulse to both bodies of a
 *        constraint: delta_pos += M^-1 * J^T * delta_lambda.
 */
static void apply_position_impulse(
    phys_vec3_t *pos_deltas,
    const phys_constraint_t *c,
    const phys_body_t *bodies,
    float delta_lambda)
{
    const phys_jacobian_row_t *row = &c->rows[0];
    uint32_t a = c->body_a;
    uint32_t b = c->body_b;

    float inv_ma = bodies[a].inv_mass;
    pos_deltas[a] = vec3_add(pos_deltas[a],
                             vec3_scale(row->J_va, inv_ma * delta_lambda));

    float inv_mb = bodies[b].inv_mass;
    pos_deltas[b] = vec3_add(pos_deltas[b],
                             vec3_scale(row->J_vb, inv_mb * delta_lambda));
}

void phys_position_projection(const phys_position_projection_args_t *args)
{
    if (!args) { return; }

    phys_position_projection_result_t *result = args->result;
    if (!result) { return; }

    result->success = false;
    result->position_deltas  = NULL;
    result->velocity_deltas  = NULL;

    const struct phys_island *island = args->island;
    if (!island || !args->constraints || !args->bodies ||
        !args->arena || args->body_count == 0) {
        return;
    }

    const uint32_t body_count = args->body_count;
    const float dt = args->dt;
    const float slop = args->slop;

    /* Use caller-provided shared arrays if available, otherwise allocate. */
    phys_vec3_t *pos_deltas = args->shared_pos_deltas;
    phys_velocity_t *vel_deltas = args->shared_vel_deltas;

    if (!pos_deltas) {
        pos_deltas = phys_frame_arena_alloc(
            args->arena, body_count * sizeof(phys_vec3_t),
            _Alignof(phys_vec3_t));
    }
    if (!vel_deltas) {
        vel_deltas = phys_frame_arena_alloc(
            args->arena, body_count * sizeof(phys_velocity_t),
            _Alignof(phys_velocity_t));
    }

    if (!pos_deltas || !vel_deltas) { return; }

    /* Zero only the island's body entries, not the entire array. */
    for (uint32_t bi = 0; bi < island->body_count; bi++) {
        uint32_t idx = island->body_indices[bi];
        pos_deltas[idx] = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        vel_deltas[idx] = (phys_velocity_t){{0.0f, 0.0f, 0.0f},
                                             {0.0f, 0.0f, 0.0f}};
    }

    result->position_deltas = pos_deltas;
    result->velocity_deltas = vel_deltas;
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
     * For each constraint, compute the residual position error and
     * apply a diagonal correction using the pre-computed effective_mass.
     * This is O(nc × iterations) instead of O(nc³) for dense LDLT. */
    for (uint32_t iter = 0; iter < PP_GS_ITERATIONS; iter++) {
        for (uint32_t i = 0; i < nc; i++) {
            if (phi[i] <= MIN_CORRECTION) { continue; }

            uint32_t ci = island->constraint_indices[i];
            const phys_constraint_t *con = &args->constraints[ci];

            /* Compute residual = phi - J * current_delta_pos. */
            float residual = compute_residual(con, pos_deltas, phi[i]);

            /* No correction needed if residual is non-positive. */
            if (residual <= MIN_CORRECTION) { continue; }

            /* Gauss-Seidel update: delta_lambda = effective_mass * residual.
             * effective_mass = 1 / (J M^-1 J^T) for this constraint's
             * diagonal entry. */
            float eff_mass = con->rows[0].effective_mass;
            if (eff_mass <= 0.0f) { continue; }

            float delta_lambda = eff_mass * residual;

            /* Clamp accumulated lambda to be non-negative (contacts push apart). */
            float new_lambda = lambda[i] + delta_lambda;
            if (new_lambda < 0.0f) { new_lambda = 0.0f; }
            delta_lambda = new_lambda - lambda[i];
            lambda[i] = new_lambda;

            if (fabsf(delta_lambda) < 1e-10f) { continue; }

            /* Apply position impulse: delta_pos += M^-1 J^T delta_lambda. */
            apply_position_impulse(pos_deltas, con, args->bodies,
                                   delta_lambda);
        }
    }

    /* ── Step 3: Velocity sync: v_delta = delta_pos / dt ───────── */
    if (dt > 0.0f) {
        float inv_dt = 1.0f / dt;
        for (uint32_t bi = 0; bi < island->body_count; bi++) {
            uint32_t idx = island->body_indices[bi];
            vel_deltas[idx].linear = vec3_scale(pos_deltas[idx], inv_dt);
        }
    }
}
