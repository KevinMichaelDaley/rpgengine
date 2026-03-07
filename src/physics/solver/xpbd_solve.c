/**
 * @file xpbd_solve.c
 * @brief XPBD (Extended Position-Based Dynamics) solver with full 6-DOF support.
 *
 * Position-and-orientation-based solver for T2-T4 bodies.  Unconditionally
 * stable, does not require island decomposition.  Handles both linear
 * (positional) and angular (orientational) constraint rows.
 *
 * For each constraint row the solver examines J_wa/J_wb to determine
 * whether angular corrections are needed.  If both angular Jacobians
 * are zero, only linear position corrections are applied.
 *
 * Derives final linear and angular velocities from position/orientation
 * deltas after solving.
 *
 * 2 non-static functions: phys_stage_xpbd_solve,
 *                         phys_xpbd_solve_constraint_batch
 */

#include "ferrum/physics/xpbd_solve.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Copy body state from input array to output array.
 */
static void copy_bodies(const phys_body_t *src, phys_body_t *dst,
                         uint32_t count)
{
    memcpy(dst, src, count * sizeof(phys_body_t));
}

/**
 * @brief Apply an angular impulse (axis-scaled delta) to a quaternion.
 *
 * Integrates a small angular correction: q' = q + 0.5 * [wx, wy, wz, 0] * q.
 * The result is re-normalized.
 *
 * @param q       Current orientation.
 * @param delta_w Angular correction vector (radians, world-space).
 * @return Updated normalized quaternion.
 */
static phys_quat_t apply_angular_correction(phys_quat_t q, phys_vec3_t delta_w)
{
    /* dq = 0.5 * (delta_w, 0) * q */
    phys_quat_t dq;
    dq.x = 0.5f * ( delta_w.x * q.w + delta_w.y * q.z - delta_w.z * q.y);
    dq.y = 0.5f * (-delta_w.x * q.z + delta_w.y * q.w + delta_w.z * q.x);
    dq.z = 0.5f * ( delta_w.x * q.y - delta_w.y * q.x + delta_w.z * q.w);
    dq.w = 0.5f * (-delta_w.x * q.x - delta_w.y * q.y - delta_w.z * q.z);

    phys_quat_t result = {
        q.x + dq.x,
        q.y + dq.y,
        q.z + dq.z,
        q.w + dq.w
    };
    return quat_normalize_safe(result, 1e-8f);
}

/**
 * @brief Compute generalized inverse mass for a constraint row.
 *
 * w = inv_mass_a * |J_va|^2 + inv_mass_b * |J_vb|^2
 *   + dot(J_wa, inv_I_a * J_wa) + dot(J_wb, inv_I_b * J_wb)
 *
 * For XPBD we use diagonal inertia in local space (inv_inertia_diag)
 * rotated to world space via the body's current orientation.
 */
static float compute_generalized_inv_mass(
    const phys_jacobian_row_t *row,
    const phys_body_t *ba,
    const phys_body_t *bb)
{
    /* Linear contribution. */
    float w = ba->inv_mass * vec3_dot(row->J_va, row->J_va)
            + bb->inv_mass * vec3_dot(row->J_vb, row->J_vb);

    /* Angular contribution for body A. */
    phys_vec3_t wa_local = quat_inv_rotate_vec3(ba->orientation, row->J_wa);
    phys_vec3_t inv_I_wa = {
        wa_local.x * ba->inv_inertia_diag.x,
        wa_local.y * ba->inv_inertia_diag.y,
        wa_local.z * ba->inv_inertia_diag.z
    };
    phys_vec3_t inv_I_wa_world = quat_rotate_vec3(ba->orientation, inv_I_wa);
    w += vec3_dot(row->J_wa, inv_I_wa_world);

    /* Angular contribution for body B. */
    phys_vec3_t wb_local = quat_inv_rotate_vec3(bb->orientation, row->J_wb);
    phys_vec3_t inv_I_wb = {
        wb_local.x * bb->inv_inertia_diag.x,
        wb_local.y * bb->inv_inertia_diag.y,
        wb_local.z * bb->inv_inertia_diag.z
    };
    phys_vec3_t inv_I_wb_world = quat_rotate_vec3(bb->orientation, inv_I_wb);
    w += vec3_dot(row->J_wb, inv_I_wb_world);

    return w;
}

/**
 * @brief Solve a single contact constraint (normal row, position only).
 */
static void solve_contact_xpbd(phys_constraint_t *c,
                                phys_body_t *bodies,
                                float omega,
                                float dt,
                                float default_compliance)
{
    phys_body_t *ba = &bodies[c->body_a];
    phys_body_t *bb = &bodies[c->body_b];

    phys_jacobian_row_t *row = &c->rows[0];

    float w = compute_generalized_inv_mass(row, ba, bb);
    if (w < 1e-10f) return;

    phys_vec3_t normal = row->J_vb;
    float n_len_sq = vec3_dot(normal, normal);
    if (n_len_sq < 1e-10f) return;

    float C = -row->bias * dt;

    float compliance = (c->compliance > 0.0f) ? c->compliance : default_compliance;
    float alpha_tilde = (dt > 0.0f) ? compliance / (dt * dt) : 0.0f;

    float delta_lambda = (-C - alpha_tilde * row->lambda) / (w + alpha_tilde);

    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < 0.0f) row->lambda = 0.0f;
    delta_lambda = row->lambda - old_lambda;

    /* Linear corrections. */
    ba->position = vec3_sub(ba->position,
        vec3_scale(row->J_va, ba->inv_mass * delta_lambda * omega));
    bb->position = vec3_sub(bb->position,
        vec3_scale(row->J_vb, -bb->inv_mass * delta_lambda * omega));
}

/**
 * @brief Solve a joint constraint with full 6-DOF (position + orientation).
 *
 * Each row's J_va/J_vb drive position corrections, and J_wa/J_wb drive
 * orientation corrections.  The generalized inverse mass accounts for
 * both linear and angular contributions.
 */
static void solve_joint_xpbd(phys_constraint_t *c,
                              phys_body_t *bodies,
                              float omega,
                              float dt,
                              float default_compliance)
{
    phys_body_t *ba = &bodies[c->body_a];
    phys_body_t *bb = &bodies[c->body_b];

    float compliance = (c->compliance > 0.0f) ? c->compliance : default_compliance;
    float alpha_tilde = (dt > 0.0f) ? compliance / (dt * dt) : 0.0f;

    for (uint8_t r = 0; r < c->row_count; ++r) {
        phys_jacobian_row_t *row = &c->rows[r];

        /* Compute generalized inverse mass (linear + angular). */
        float w = compute_generalized_inv_mass(row, ba, bb);
        if (w < 1e-10f) continue;

        float C = row->bias;
        float delta_lambda = (-C - alpha_tilde * row->lambda) / (w + alpha_tilde);

        /* Bilateral clamp. */
        float old_lambda = row->lambda;
        row->lambda = old_lambda + delta_lambda;
        if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
        if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
        delta_lambda = row->lambda - old_lambda;

        float scaled_dl = delta_lambda * omega;

        /* Linear position corrections: dp = inv_mass * J_v * delta_lambda.
         * Body A uses J_va (typically -normal), body B uses J_vb (+normal).
         * The Jacobian encodes the sign, so we subtract for both. */
        if (ba->inv_mass > 0.0f) {
            ba->position = vec3_add(ba->position,
                vec3_scale(row->J_va, ba->inv_mass * scaled_dl));
        }
        if (bb->inv_mass > 0.0f) {
            bb->position = vec3_add(bb->position,
                vec3_scale(row->J_vb, bb->inv_mass * scaled_dl));
        }

        /* Angular orientation corrections: dq from inv_I * J_w * delta_lambda.
         * Transform J_w to local, scale by diagonal inv_inertia, back to world. */
        float jwa_sq = vec3_dot(row->J_wa, row->J_wa);
        if (jwa_sq > 1e-12f && ba->inv_mass > 0.0f) {
            phys_vec3_t wa_local = quat_inv_rotate_vec3(ba->orientation, row->J_wa);
            phys_vec3_t dw_local = {
                wa_local.x * ba->inv_inertia_diag.x * scaled_dl,
                wa_local.y * ba->inv_inertia_diag.y * scaled_dl,
                wa_local.z * ba->inv_inertia_diag.z * scaled_dl
            };
            phys_vec3_t dw_world = quat_rotate_vec3(ba->orientation, dw_local);
            ba->orientation = apply_angular_correction(ba->orientation, dw_world);
        }

        float jwb_sq = vec3_dot(row->J_wb, row->J_wb);
        if (jwb_sq > 1e-12f && bb->inv_mass > 0.0f) {
            phys_vec3_t wb_local = quat_inv_rotate_vec3(bb->orientation, row->J_wb);
            phys_vec3_t dw_local = {
                wb_local.x * bb->inv_inertia_diag.x * scaled_dl,
                wb_local.y * bb->inv_inertia_diag.y * scaled_dl,
                wb_local.z * bb->inv_inertia_diag.z * scaled_dl
            };
            phys_vec3_t dw_world = quat_rotate_vec3(bb->orientation, dw_local);
            bb->orientation = apply_angular_correction(bb->orientation, dw_world);
        }

        /* Bias is NOT updated here — joints are rebuilt from actual
         * positions each iteration, giving a fresh C value. */
    }
}

/**
 * @brief Derive linear and angular velocities from position/orientation deltas.
 *
 * Linear:  v = (pos_out - pos_in) / dt
 * Angular: w = 2 * (q_out * q_in^-1).xyz / dt  (small-angle approximation)
 */
static void derive_velocities(const phys_body_t *bodies_in,
                               const phys_body_t *bodies_out,
                               phys_velocity_t *velocities,
                               uint32_t body_count,
                               float dt)
{
    float inv_dt = 1.0f / dt;
    for (uint32_t i = 0; i < body_count; i++) {
        /* Linear velocity from position delta. */
        phys_vec3_t dp = vec3_sub(bodies_out[i].position,
                                   bodies_in[i].position);
        velocities[i].linear = vec3_scale(dp, inv_dt);

        /* Angular velocity from orientation delta.
         * dq = q_out * conj(q_in), then w ≈ 2 * dq.xyz / dt */
        phys_quat_t q_in_conj = quat_conjugate(bodies_in[i].orientation);
        phys_quat_t dq = quat_mul(bodies_out[i].orientation, q_in_conj);
        /* Ensure shortest path (w component positive). */
        float sign = (dq.w >= 0.0f) ? 1.0f : -1.0f;
        velocities[i].angular = (phys_vec3_t){
            2.0f * dq.x * sign * inv_dt,
            2.0f * dq.y * sign * inv_dt,
            2.0f * dq.z * sign * inv_dt
        };
    }
}

/* ── Public API ─────────────────────────────────────────────────── */

void phys_stage_xpbd_solve(const phys_xpbd_solve_args_t *args)
{
    if (!args || !args->constraints) return;
    if (!args->bodies_in || !args->bodies_out) return;
    if (args->body_count == 0 || args->constraint_count == 0) return;

    /* Step 1: Copy body state from input to output workspace. */
    copy_bodies(args->bodies_in, args->bodies_out, args->body_count);

    /* Step 2: Iterative position+orientation constraint projection. */
    for (uint32_t iter = 0; iter < args->iterations; iter++) {
        for (uint32_t ci = 0; ci < args->constraint_count; ci++) {
            phys_constraint_t *c = &args->constraints[ci];
            if (c->is_joint) {
                solve_joint_xpbd(c, args->bodies_out,
                                 args->omega, args->dt,
                                 args->compliance);
            } else {
                solve_contact_xpbd(c, args->bodies_out,
                                   args->omega, args->dt,
                                   args->compliance);
            }
        }
    }

    /* Step 3: Derive velocities from position+orientation change. */
    if (args->velocities_out && args->dt > 0.0f) {
        derive_velocities(args->bodies_in, args->bodies_out,
                          args->velocities_out, args->body_count,
                          args->dt);
    }
}

void phys_xpbd_solve_constraint_batch(
    phys_constraint_t *constraints,
    uint32_t count,
    phys_body_t *bodies,
    uint32_t iterations,
    float omega,
    float dt,
    float compliance)
{
    if (!constraints || !bodies || count == 0) return;

    for (uint32_t iter = 0; iter < iterations; iter++) {
        for (uint32_t ci = 0; ci < count; ci++) {
            phys_constraint_t *c = &constraints[ci];
            if (c->is_joint) {
                solve_joint_xpbd(c, bodies, omega, dt, compliance);
            } else {
                solve_contact_xpbd(c, bodies, omega, dt, compliance);
            }
        }
    }
}
