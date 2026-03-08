/**
 * @file xpbd_solve_par.c
 * @brief Parallel XPBD position solver implementation.
 *
 * Splits constraint range into batches of PHYS_XPBD_SOLVE_BATCH_SIZE,
 * dispatches each batch as a job that solves its constraint slice using
 * Jacobi iteration (reads from start-of-iteration body positions,
 * writes corrections to its own body slots).
 *
 * Non-static functions: 1 (phys_stage_xpbd_solve_par).
 */

#include "ferrum/physics/par/xpbd_solve_par.h"

#include <stddef.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* ── Per-batch shared context ──────────────────────────────────── */

/**
 * @brief Shared context across all batches in a parallel XPBD solve.
 *
 * Each job receives a phys_job_batch_t whose user_args points here.
 * All jobs read from the same bodies array (Jacobi: read from
 * start-of-iteration positions) and write corrections back.
 */
typedef struct xpbd_solve_shared {
    phys_constraint_t *constraints;  /**< Constraint array (read/write). */
    phys_body_t       *bodies;       /**< Working body positions (read/write). */
    float              omega;        /**< Jacobi relaxation factor. */
    float              dt;           /**< Timestep in seconds. */
    float              compliance;   /**< XPBD compliance (α); 0 = stiff. */
} xpbd_solve_shared_t;

/* ── Static helpers ────────────────────────────────────────────── */

/**
 * @brief Compute generalized inverse mass for a Jacobian row.
 *
 * Includes both linear (m^-1 * J_v · J_v) and angular
 * (J_w · I^-1 · J_w) terms, with diagonal inverse inertia
 * rotated to world space via the body's current orientation.
 */
static float compute_generalized_inv_mass_(
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
 * @brief Apply angular correction to a quaternion.
 *
 * q' = normalize(q + 0.5 * (dw, 0) * q).
 */
static phys_quat_t apply_angular_correction_(phys_quat_t q, phys_vec3_t dw)
{
    phys_quat_t dq = quat_mul(
        (phys_quat_t){ dw.x, dw.y, dw.z, 0.0f }, q);
    q.x += 0.5f * dq.x;
    q.y += 0.5f * dq.y;
    q.z += 0.5f * dq.z;
    q.w += 0.5f * dq.w;
    return quat_normalize_safe(q, 1e-8f);
}

/**
 * @brief Solve a single contact constraint at the position level.
 */
static void solve_contact_position(phys_constraint_t *c,
                                   phys_body_t *bodies,
                                   float omega,
                                   float dt,
                                   float compliance)
{
    phys_body_t *ba = &bodies[c->body_a];
    phys_body_t *bb = &bodies[c->body_b];

    /* Only solve the normal row (row 0) for position correction. */
    phys_jacobian_row_t *row = &c->rows[0];

    /* Full generalized inverse mass (linear + angular). */
    float w = compute_generalized_inv_mass_(row, ba, bb);
    if (w < 1e-10f) return;

    phys_vec3_t normal = row->J_vb;
    float n_len_sq = vec3_dot(normal, normal);
    if (n_len_sq < 1e-10f) return;

    /* XPBD needs a position-level error, not the velocity-space bias used
     * by the TGS solver. For actual overlap, solve the penetration depth
     * directly; otherwise keep the speculative gap bias. */
    float C = (c->penetration > 0.0f)
        ? -c->penetration
        : (-row->bias * dt);

    float c_alpha = (c->compliance > 0.0f) ? c->compliance : compliance;
    float alpha_tilde = (dt > 0.0f) ? c_alpha / (dt * dt) : 0.0f;

    float delta_lambda = (-C - alpha_tilde * row->lambda) / (w + alpha_tilde);

    /* Clamp: contacts only push apart (accumulated lambda >= 0). */
    float old_lambda = row->lambda;
    row->lambda = old_lambda + delta_lambda;
    if (row->lambda < 0.0f) row->lambda = 0.0f;
    delta_lambda = row->lambda - old_lambda;

    float scaled_dl = delta_lambda * omega;

    /* Linear position corrections using proper Jacobians. */
    if (ba->inv_mass > 0.0f) {
        ba->position = vec3_add(ba->position,
            vec3_scale(row->J_va, ba->inv_mass * scaled_dl));
    }
    if (bb->inv_mass > 0.0f) {
        bb->position = vec3_add(bb->position,
            vec3_scale(row->J_vb, bb->inv_mass * scaled_dl));
    }

    float jwa_sq = vec3_dot(row->J_wa, row->J_wa);
    if (jwa_sq > 1e-12f && ba->inv_mass > 0.0f) {
        phys_vec3_t wa_local = quat_inv_rotate_vec3(ba->orientation, row->J_wa);
        phys_vec3_t dw_local = {
            wa_local.x * ba->inv_inertia_diag.x * scaled_dl,
            wa_local.y * ba->inv_inertia_diag.y * scaled_dl,
            wa_local.z * ba->inv_inertia_diag.z * scaled_dl
        };
        phys_vec3_t dw_world = quat_rotate_vec3(ba->orientation, dw_local);
        ba->orientation = apply_angular_correction_(ba->orientation, dw_world);
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
        bb->orientation = apply_angular_correction_(bb->orientation, dw_world);
    }
}

/**
 * @brief Solve a joint constraint at the position level (all rows, 6-DOF).
 *
 * Matches the sequential solve_joint_xpbd: every row applies both
 * linear position corrections (via J_va/J_vb) and angular orientation
 * corrections (via J_wa/J_wb with diagonal inverse inertia rotated
 * through the body quaternion).
 */
static void solve_joint_position(phys_constraint_t *c,
                                 phys_body_t *bodies,
                                 float omega,
                                 float dt,
                                 float compliance)
{
    phys_body_t *ba = &bodies[c->body_a];
    phys_body_t *bb = &bodies[c->body_b];

    /* Minimum compliance floor: prevents spectral-radius divergence
     * in stiff coupled joint chains.  See xpbd_solve.c for rationale. */
    float c_alpha = (c->compliance > 0.0f) ? c->compliance : compliance;
    if (c_alpha < compliance) c_alpha = compliance;
    float alpha_tilde = (dt > 0.0f) ? c_alpha / (dt * dt) : 0.0f;

    /* XPBD-D: damping coefficient γ = α̃ · d · h.
     * Adds velocity-opposing term so joints dissipate kinetic energy.
     * When joint_damping = 0 this vanishes → standard XPBD. */
    float gamma = 0.0f;
    if (c->joint_damping > 0.0f && dt > 0.0f) {
        gamma = alpha_tilde * c->joint_damping * dt;
    }

    for (uint8_t r = 0; r < c->row_count; ++r) {
        phys_jacobian_row_t *row = &c->rows[r];

        /* Full generalized inverse mass (linear + angular). */
        float w = compute_generalized_inv_mass_(row, ba, bb);
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

        float C = row->bias;
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

        /* Linear position corrections: dp = inv_mass * J_v * Δλ. */
        if (ba->inv_mass > 0.0f) {
            ba->position = vec3_add(ba->position,
                vec3_scale(row->J_va, ba->inv_mass * scaled_dl));
        }
        if (bb->inv_mass > 0.0f) {
            bb->position = vec3_add(bb->position,
                vec3_scale(row->J_vb, bb->inv_mass * scaled_dl));
        }

        /* Angular orientation corrections: dθ = I^-1 · J_w · Δλ. */
        float jwa_sq = vec3_dot(row->J_wa, row->J_wa);
        if (jwa_sq > 1e-12f && ba->inv_mass > 0.0f) {
            phys_vec3_t wa_local = quat_inv_rotate_vec3(ba->orientation, row->J_wa);
            phys_vec3_t dw_local = {
                wa_local.x * ba->inv_inertia_diag.x * scaled_dl,
                wa_local.y * ba->inv_inertia_diag.y * scaled_dl,
                wa_local.z * ba->inv_inertia_diag.z * scaled_dl
            };
            phys_vec3_t dw_world = quat_rotate_vec3(ba->orientation, dw_local);
            ba->orientation = apply_angular_correction_(ba->orientation, dw_world);
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
            bb->orientation = apply_angular_correction_(bb->orientation, dw_world);
        }
    }
}

/**
 * @brief Job function: solve constraints in [start, start+count).
 */
static void xpbd_batch_job(void *data)
{
    phys_job_batch_t *batch = data;
    xpbd_solve_shared_t *shared = batch->user_args;

    uint32_t end = batch->start + batch->count;
    for (uint32_t ci = batch->start; ci < end; ++ci) {
        phys_constraint_t *c = &shared->constraints[ci];
        if (c->is_joint) {
            solve_joint_position(c, shared->bodies,
                                 shared->omega, shared->dt,
                                 shared->compliance);
        } else {
            solve_contact_position(c, shared->bodies,
                                   shared->omega, shared->dt,
                                   shared->compliance);
        }
    }
}

/**
 * @brief Derive velocities from position/orientation deltas after solving.
 *
 * Linear velocity = Δposition / dt.
 * Angular velocity = 2 * (q_out * conj(q_in)).xyz / dt.
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

        /* Angular velocity from quaternion delta:
         *   dq = q_out * conj(q_in)
         *   ω  = 2 * dq.xyz / dt  (when dq.w > 0) */
        phys_quat_t q_in  = bodies_in[i].orientation;
        phys_quat_t q_out = bodies_out[i].orientation;
        phys_quat_t q_in_conj = { -q_in.x, -q_in.y, -q_in.z, q_in.w };
        phys_quat_t dq = quat_mul(q_out, q_in_conj);

        /* Ensure shortest path (dq.w >= 0). */
        float sign = (dq.w >= 0.0f) ? 1.0f : -1.0f;
        velocities[i].angular = (phys_vec3_t){
            2.0f * sign * dq.x * inv_dt,
            2.0f * sign * dq.y * inv_dt,
            2.0f * sign * dq.z * inv_dt
        };
    }
}

/* ── Public API ────────────────────────────────────────────────── */

void phys_stage_xpbd_solve_par(const phys_xpbd_solve_args_t *args,
                                phys_job_context_t *ctx,
                                phys_frame_arena_t *arena)
{
    if (!args || !ctx || !arena) {
        /* Fall back to sequential if no job context. */
        if (args && (!ctx || !arena)) {
            phys_stage_xpbd_solve(args);
        }
        return;
    }

    if (!args->constraints || !args->bodies_in || !args->bodies_out) {
        return;
    }
    if (args->body_count == 0 || args->constraint_count == 0) {
        /* Zero constraints: copy bodies through and derive velocities. */
        if (args->body_count > 0 && args->bodies_in && args->bodies_out) {
            memcpy(args->bodies_out, args->bodies_in,
                   args->body_count * sizeof(phys_body_t));
            if (args->velocities_out && args->dt > 0.0f) {
                derive_velocities(args->bodies_in, args->bodies_out,
                                  args->velocities_out, args->body_count,
                                  args->dt);
            }
        }
        return;
    }

    /* Step 1: Copy body state from input to output workspace. */
    memcpy(args->bodies_out, args->bodies_in,
           args->body_count * sizeof(phys_body_t));

    /* Set up shared context for constraint batches. */
    xpbd_solve_shared_t shared = {
        .constraints = args->constraints,
        .bodies      = args->bodies_out,
        .omega       = args->omega,
        .dt          = args->dt,
        .compliance  = args->compliance,
    };

    /* Step 2: Solve serially. The old batch path wrote shared body state from
     * multiple jobs at once, which races whenever constraints in the same
     * island share bodies. */
    phys_job_batch_t batch = {
        .user_args = &shared,
        .start = 0,
        .count = args->constraint_count,
        .batch_idx = 0,
    };
    for (uint32_t iter = 0; iter < args->iterations; iter++) {
        xpbd_batch_job(&batch);
    }

    /* Step 3: Derive velocities from position change. */
    if (args->velocities_out && args->dt > 0.0f) {
        derive_velocities(args->bodies_in, args->bodies_out,
                          args->velocities_out, args->body_count,
                          args->dt);
    }
}
