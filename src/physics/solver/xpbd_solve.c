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

/* Maximum per-iteration correction magnitudes.  Without these clamps,
 * a single deep penetration or large constraint error can produce an
 * arbitrarily large position/orientation jump, injecting energy into
 * the system and destabilizing coupled joint chains. */
#define XPBD_MAX_LINEAR_CORRECTION  0.05f   /* meters per iteration */
#define XPBD_MAX_ANGULAR_CORRECTION 0.02f   /* radians per iteration (~1.1°) —
                                             * small enough that the linear
                                             * Jacobian approximation holds. */

/* ── Static helpers ─────────────────────────────────────────────── */

/**
 * @brief Clamp a vector's magnitude to a maximum length.
 * @return The (possibly shortened) vector.
 */
static phys_vec3_t clamp_vec3_magnitude(phys_vec3_t v, float max_len)
{
    float sq = vec3_magnitude_sq(v);
    if (sq > max_len * max_len) {
        float scale = max_len / sqrtf(sq);
        return vec3_scale(v, scale);
    }
    return v;
}

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
    result = quat_normalize_safe(result, 1e-8f);
    /* Shortest-path check: ensure the corrected quaternion is in the
     * same hemisphere as the input.  If dot(q, result) < 0 the solver
     * jumped to the antipodal representation, which would look like a
     * ~π rotation error to downstream constraint builders.  Negating
     * result maps it back to the same rotation without changing the
     * physical orientation. */
    float dot = q.x * result.x + q.y * result.y
              + q.z * result.z + q.w * result.w;
    if (dot < 0.0f) {
        result.x = -result.x;
        result.y = -result.y;
        result.z = -result.z;
        result.w = -result.w;
    }
    return result;
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
 * @brief Solve a single contact constraint (linear-only position correction).
 *
 * Contact rows are linear constraints (normal + friction tangents).
 * The lever-arm angular Jacobians (J_wa, J_wb = r×n) are excluded
 * from both the generalized inverse mass and the correction to avoid
 * injecting angular energy that fights joint constraints.
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

    phys_vec3_t normal = row->J_vb;
    float n_len_sq = vec3_dot(normal, normal);
    if (n_len_sq < 1e-10f) return;

    /* Linear-only generalized inverse mass (no angular lever-arm term). */
    float w = ba->inv_mass * vec3_dot(row->J_va, row->J_va)
            + bb->inv_mass * vec3_dot(row->J_vb, row->J_vb);
    if (w < 1e-10f) return;

    /* XPBD is a position-level solver — only correct actual overlaps.
     * Speculative contacts (penetration <= 0) are a velocity-level concept
     * and must be skipped here; otherwise the correction pulls bodies
     * *toward* each other. */
    if (c->penetration <= 0.0f) return;
    float C = -c->penetration;

    float compliance = (c->compliance > 0.0f) ? c->compliance : default_compliance;
    float alpha_tilde = (dt > 0.0f) ? compliance / (dt * dt) : 0.0f;

    float delta_lambda = (-C - alpha_tilde * row->lambda) / (w + alpha_tilde);

    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < 0.0f) row->lambda = 0.0f;
    delta_lambda = row->lambda - old_lambda;

    /* Linear corrections only — no angular lever-arm correction.
     * Clamped to prevent energy injection from deep penetrations. */
    if (ba->inv_mass > 0.0f) {
        phys_vec3_t dp = vec3_scale(row->J_va, ba->inv_mass * delta_lambda * omega);
        dp = clamp_vec3_magnitude(dp, XPBD_MAX_LINEAR_CORRECTION);
        ba->position = vec3_add(ba->position, dp);
    }
    if (bb->inv_mass > 0.0f) {
        phys_vec3_t dp = vec3_scale(row->J_vb, bb->inv_mass * delta_lambda * omega);
        dp = clamp_vec3_magnitude(dp, XPBD_MAX_LINEAR_CORRECTION);
        bb->position = vec3_add(bb->position, dp);
    }
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

    /* Minimum compliance floor: without sufficient compliance, α̃ → 0
     * and XPBD degenerates to raw Jacobi projection.  For coupled
     * joint chains the iteration matrix's spectral radius can exceed 1,
     * causing divergence.  The floor is applied via default_compliance
     * which is set from world->config.xpbd_min_compliance upstream.
     * Per-constraint compliance is also floored to the default so that
     * no joint can be stiffer than the system can converge. */
    float compliance = (c->compliance > 0.0f) ? c->compliance : default_compliance;
    if (compliance < default_compliance) compliance = default_compliance;
    float alpha_tilde = (dt > 0.0f) ? compliance / (dt * dt) : 0.0f;

    /* XPBD-D: damping coefficient γ = α̃ · d · h.
     * Adds velocity-opposing term so joints dissipate kinetic energy.
     * When joint_damping = 0 this vanishes → standard XPBD. */
    float gamma = 0.0f;
    if (c->joint_damping > 0.0f && dt > 0.0f) {
        gamma = alpha_tilde * c->joint_damping * dt;
    }

    for (uint8_t r = 0; r < c->row_count; ++r) {
        phys_jacobian_row_t *row = &c->rows[r];

        /* Compute generalized inverse mass (linear + angular). */
        float w = compute_generalized_inv_mass(row, ba, bb);
        if (w < 1e-10f) continue;

        /* Relative velocity along constraint direction: J·v.
         * Only computed when damping is active (gamma > 0). */
        float jv = 0.0f;
        if (gamma > 0.0f) {
            jv = vec3_dot(row->J_va, ba->linear_vel)
               + vec3_dot(row->J_wa, ba->angular_vel)
               + vec3_dot(row->J_vb, bb->linear_vel)
               + vec3_dot(row->J_wb, bb->angular_vel);
        }

        float C = row->constraint_error;
        /* XPBD-D: Δλ = -(C + α̃·λ + γ·Jv) / ((1+γ)·w + α̃) */
        float delta_lambda = -(C + alpha_tilde * row->lambda + gamma * jv)
                           / ((1.0f + gamma) * w + alpha_tilde);

        /* Bilateral clamp. */
        float old_lambda = row->lambda;
        row->lambda = old_lambda + delta_lambda;
        if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
        if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
        delta_lambda = row->lambda - old_lambda;

        float scaled_dl = delta_lambda * omega;

        /* Linear position corrections: dp = inv_mass * J_v * delta_lambda.
         * Body A uses J_va (typically -normal), body B uses J_vb (+normal).
         * The Jacobian encodes the sign, so we subtract for both.
         * Clamped to prevent energy injection from large errors. */
        if (ba->inv_mass > 0.0f) {
            phys_vec3_t dp = vec3_scale(row->J_va, ba->inv_mass * scaled_dl);
            dp = clamp_vec3_magnitude(dp, XPBD_MAX_LINEAR_CORRECTION);
            ba->position = vec3_add(ba->position, dp);
        }
        if (bb->inv_mass > 0.0f) {
            phys_vec3_t dp = vec3_scale(row->J_vb, bb->inv_mass * scaled_dl);
            dp = clamp_vec3_magnitude(dp, XPBD_MAX_LINEAR_CORRECTION);
            bb->position = vec3_add(bb->position, dp);
        }

        /* Angular orientation corrections: dq from inv_I * J_w * delta_lambda.
         * Transform J_w to local, scale by diagonal inv_inertia, back to world.
         * Clamped so the linear Jacobian approximation stays valid. */
        float jwa_sq = vec3_dot(row->J_wa, row->J_wa);
        if (jwa_sq > 1e-12f && ba->inv_mass > 0.0f) {
            phys_vec3_t wa_local = quat_inv_rotate_vec3(ba->orientation, row->J_wa);
            phys_vec3_t dw_local = {
                wa_local.x * ba->inv_inertia_diag.x * scaled_dl,
                wa_local.y * ba->inv_inertia_diag.y * scaled_dl,
                wa_local.z * ba->inv_inertia_diag.z * scaled_dl
            };
            phys_vec3_t dw_world = quat_rotate_vec3(ba->orientation, dw_local);
            dw_world = clamp_vec3_magnitude(dw_world, XPBD_MAX_ANGULAR_CORRECTION);
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
            dw_world = clamp_vec3_magnitude(dw_world, XPBD_MAX_ANGULAR_CORRECTION);
            bb->orientation = apply_angular_correction(bb->orientation, dw_world);
        }

        /* Bias is NOT updated here — joints are rebuilt from actual
         * positions each iteration, giving a fresh C value. */
    }
}

/* Damping factor applied to XPBD-derived velocities.
 * Aggressively damps the constraint-correction velocities to prevent
 * angular energy accumulation from joint/contact coupling.  This does
 * NOT affect gravity or external forces — only the velocity component
 * that comes from XPBD position/orientation corrections. */
#define XPBD_LINEAR_VELOCITY_DAMPING  0.7f
#define XPBD_ANGULAR_VELOCITY_DAMPING 0.2f

/**
 * @brief Derive linear and angular velocities from position/orientation deltas.
 *
 * Linear:  v = (pos_out - pos_in) / dt * damping
 * Angular: w = 2 * (q_out * q_in^-1).xyz / dt * damping
 */
static void derive_velocities(const phys_body_t *bodies_in,
                               const phys_body_t *bodies_out,
                               phys_velocity_t *velocities,
                               uint32_t body_count,
                               float dt)
{
    float inv_dt = 1.0f / dt;
    for (uint32_t i = 0; i < body_count; i++) {
        /* Linear velocity from position delta, damped. */
        phys_vec3_t dp = vec3_sub(bodies_out[i].position,
                                   bodies_in[i].position);
        velocities[i].linear = vec3_scale(dp,
            inv_dt * XPBD_LINEAR_VELOCITY_DAMPING);

        /* Angular velocity from orientation delta, aggressively damped.
         * dq = q_out * conj(q_in), then w ≈ 2 * dq.xyz / dt */
        phys_quat_t q_in_conj = quat_conjugate(bodies_in[i].orientation);
        phys_quat_t dq = quat_mul(bodies_out[i].orientation, q_in_conj);
        /* Ensure shortest path (w component positive). */
        float sign = (dq.w >= 0.0f) ? 1.0f : -1.0f;
        float ang_scale = 2.0f * sign * inv_dt * XPBD_ANGULAR_VELOCITY_DAMPING;
        velocities[i].angular = (phys_vec3_t){
            dq.x * ang_scale,
            dq.y * ang_scale,
            dq.z * ang_scale
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

    /* Step 2: Iterative constraint projection.
     * Joints first (angular + linear), then contacts (linear only).
     * Solving contacts last prevents lever-arm energy injection from
     * fighting joint angular corrections. */
    for (uint32_t iter = 0; iter < args->iterations; iter++) {
        /* Pass 1: joints. */
        for (uint32_t ci = 0; ci < args->constraint_count; ci++) {
            phys_constraint_t *c = &args->constraints[ci];
            if (c->is_joint) {
                solve_joint_xpbd(c, args->bodies_out,
                                 args->omega, args->dt,
                                 args->compliance);
            }
        }
        /* Pass 2: contacts. */
        for (uint32_t ci = 0; ci < args->constraint_count; ci++) {
            phys_constraint_t *c = &args->constraints[ci];
            if (!c->is_joint) {
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
        /* Pass 1: joints. */
        for (uint32_t ci = 0; ci < count; ci++) {
            phys_constraint_t *c = &constraints[ci];
            if (c->is_joint) {
                solve_joint_xpbd(c, bodies, omega, dt, compliance);
            }
        }
        /* Pass 2: contacts. */
        for (uint32_t ci = 0; ci < count; ci++) {
            phys_constraint_t *c = &constraints[ci];
            if (!c->is_joint) {
                solve_contact_xpbd(c, bodies, omega, dt, compliance);
            }
        }
    }
}
