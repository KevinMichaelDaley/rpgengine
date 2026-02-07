/**
 * @file position_projection.c
 * @brief Per-island position projection: assemble J, Phi, form A=JM^-1J^T,
 *        solve, and apply corrections.
 *
 * Non-static functions (2):
 *   1. phys_position_projection — main entry point
 *
 * (phys_dense_ldlt_solve is in ldlt_solve.c)
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
    /* Return negative Phi: constraint violation is negative
     * (bodies are overlapping, gap is negative). */
    return -excess;
}

/**
 * @brief Apply M^-1 * J^T * lambda to compute position deltas for one
 *        constraint row.
 */
static void accumulate_position_delta(
    phys_vec3_t *pos_deltas,
    const phys_constraint_t *c,
    const phys_body_t *bodies,
    float lambda_val)
{
    const phys_jacobian_row_t *row = &c->rows[0];
    uint32_t a = c->body_a;
    uint32_t b = c->body_b;

    /* delta_pos_a += inv_mass_a * J_va^T * lambda */
    float inv_ma = bodies[a].inv_mass;
    pos_deltas[a] = vec3_add(pos_deltas[a],
                             vec3_scale(row->J_va, inv_ma * lambda_val));

    /* delta_pos_b += inv_mass_b * J_vb^T * lambda */
    float inv_mb = bodies[b].inv_mass;
    pos_deltas[b] = vec3_add(pos_deltas[b],
                             vec3_scale(row->J_vb, inv_mb * lambda_val));
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

    /* Allocate output arrays from arena. */
    phys_vec3_t *pos_deltas = phys_frame_arena_alloc(
        args->arena, body_count * sizeof(phys_vec3_t),
        _Alignof(phys_vec3_t));
    phys_velocity_t *vel_deltas = phys_frame_arena_alloc(
        args->arena, body_count * sizeof(phys_velocity_t),
        _Alignof(phys_velocity_t));

    if (!pos_deltas || !vel_deltas) { return; }

    memset(pos_deltas, 0, body_count * sizeof(phys_vec3_t));
    memset(vel_deltas, 0, body_count * sizeof(phys_velocity_t));

    result->position_deltas = pos_deltas;
    result->velocity_deltas = vel_deltas;
    result->success = true;

    /* Sleeping islands produce zero corrections. */
    if (island->sleeping) { return; }

    const uint32_t nc = island->constraint_count;
    if (nc == 0) { return; }

    /* ── Step 1: Assemble Phi vector (penetration depths) ──────── */
    float *phi = phys_frame_arena_alloc(
        args->arena, nc * sizeof(float), _Alignof(float));
    if (!phi) { return; }

    uint32_t active_count = 0;
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t ci = island->constraint_indices[i];
        phi[i] = compute_phi(&args->constraints[ci], slop);
        if (phi[i] < -MIN_CORRECTION) { active_count++; }
    }

    /* If no active penetrations, we're done (zero corrections). */
    if (active_count == 0) { return; }

    /* ── Step 2: Build A = J M^-1 J^T (dense, nc × nc) ────────── */
    float *A = phys_frame_arena_alloc(
        args->arena, nc * nc * sizeof(float), _Alignof(float));
    if (!A) { return; }
    memset(A, 0, nc * nc * sizeof(float));

    /* For each pair of constraints (i, j), A[i][j] = sum over shared
     * bodies of J_i_body * M^-1_body * J_j_body^T.
     *
     * Each normal constraint row has:
     *   J_va (linear A), J_wa (angular A), J_vb (linear B), J_wb (angular B)
     *
     * For body k shared between constraints i and j:
     *   contribution = inv_mass_k * dot(J_i_vk, J_j_vk)
     *                + dot(inv_I_k * J_i_wk, J_j_wk)
     *
     * This is symmetric: A[i][j] = A[j][i]. */
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t ci = island->constraint_indices[i];
        const phys_constraint_t *ci_con = &args->constraints[ci];
        const phys_jacobian_row_t *ri = &ci_con->rows[0];

        for (uint32_t j = i; j < nc; j++) {
            uint32_t cj = island->constraint_indices[j];
            const phys_constraint_t *cj_con = &args->constraints[cj];
            const phys_jacobian_row_t *rj = &cj_con->rows[0];

            float val = 0.0f;

            /* Check body A of constraint i against both bodies of constraint j. */
            uint32_t ai = ci_con->body_a;
            uint32_t bi = ci_con->body_b;
            uint32_t aj = cj_con->body_a;
            uint32_t bj = cj_con->body_b;

            /* Body ai shared with constraint j? */
            if (ai == aj) {
                float im = args->bodies[ai].inv_mass;
                const phys_vec3_t *ii = &args->bodies[ai].inv_inertia_diag;
                val += im * vec3_dot(ri->J_va, rj->J_va);
                val += ii->x * ri->J_wa.x * rj->J_wa.x
                     + ii->y * ri->J_wa.y * rj->J_wa.y
                     + ii->z * ri->J_wa.z * rj->J_wa.z;
            } else if (ai == bj) {
                float im = args->bodies[ai].inv_mass;
                const phys_vec3_t *ii = &args->bodies[ai].inv_inertia_diag;
                val += im * vec3_dot(ri->J_va, rj->J_vb);
                val += ii->x * ri->J_wa.x * rj->J_wb.x
                     + ii->y * ri->J_wa.y * rj->J_wb.y
                     + ii->z * ri->J_wa.z * rj->J_wb.z;
            }

            /* Body bi shared with constraint j? */
            if (bi == aj) {
                float im = args->bodies[bi].inv_mass;
                const phys_vec3_t *ii = &args->bodies[bi].inv_inertia_diag;
                val += im * vec3_dot(ri->J_vb, rj->J_va);
                val += ii->x * ri->J_wb.x * rj->J_wa.x
                     + ii->y * ri->J_wb.y * rj->J_wa.y
                     + ii->z * ri->J_wb.z * rj->J_wa.z;
            } else if (bi == bj) {
                float im = args->bodies[bi].inv_mass;
                const phys_vec3_t *ii = &args->bodies[bi].inv_inertia_diag;
                val += im * vec3_dot(ri->J_vb, rj->J_vb);
                val += ii->x * ri->J_wb.x * rj->J_wb.x
                     + ii->y * ri->J_wb.y * rj->J_wb.y
                     + ii->z * ri->J_wb.z * rj->J_wb.z;
            }

            A[i * nc + j] = val;
            A[j * nc + i] = val;
        }
    }

    /* ── Step 3: Solve A * lambda = -Phi (negate Phi for RHS) ──── */
    float *rhs = phys_frame_arena_alloc(
        args->arena, nc * sizeof(float), _Alignof(float));
    float *lambda = phys_frame_arena_alloc(
        args->arena, nc * sizeof(float), _Alignof(float));
    if (!rhs || !lambda) { return; }

    for (uint32_t i = 0; i < nc; i++) {
        rhs[i] = -phi[i];
    }

    bool solved = phys_dense_ldlt_solve(A, rhs, lambda, nc);
    if (!solved) { return; }

    /* Clamp lambda to be non-negative (contacts can only push apart).
     * With Phi < 0 (penetration), RHS = -Phi > 0, so lambda > 0. */
    for (uint32_t i = 0; i < nc; i++) {
        if (lambda[i] < 0.0f) {
            lambda[i] = 0.0f;
        }
    }

    /* ── Step 4: Apply position corrections: Δq = M^-1 J^T λ ──── */
    for (uint32_t i = 0; i < nc; i++) {
        if (fabsf(lambda[i]) < 1e-10f) { continue; }
        uint32_t ci = island->constraint_indices[i];
        accumulate_position_delta(pos_deltas, &args->constraints[ci],
                                  args->bodies, lambda[i]);
    }

    /* ── Step 5: Velocity sync: v_delta = Δq / dt ─────────────── */
    if (dt > 0.0f) {
        float inv_dt = 1.0f / dt;
        for (uint32_t bi = 0; bi < island->body_count; bi++) {
            uint32_t idx = island->body_indices[bi];
            vel_deltas[idx].linear = vec3_scale(pos_deltas[idx], inv_dt);
            /* Angular velocity sync would require computing angular
             * deltas from the Jacobian angular rows; for contacts the
             * angular contribution is typically small and we omit it
             * in the first version. */
        }
    }
}
