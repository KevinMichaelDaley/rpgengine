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
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_color.h"
#include "ferrum/physics/constraint_rebuild.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/solver/cg_solve.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>

/** Minimum penetration excess to correct (avoids micro-jitter). */
#define SPLIT_MIN_PHI 1e-6f

/** Position correction ERP: fraction of penetration resolved per substep.
 *  1.0 = full correction (aggressive, can oscillate).
 *  0.2–0.4 = typical for stable stacking/resting contacts. */
#define SPLIT_ERP 0.1f

/** Speed (m/s) above which we start adding solver iterations. */
#define ADAPTIVE_SPEED_LOW  5.0f
/** Speed (m/s) at which we reach maximum solver iterations. */
#define ADAPTIVE_SPEED_HIGH 200.0f
/** Maximum multiplier on base iteration count for fast islands. */
#define ADAPTIVE_ITER_MULT  5

/** Successive over-relaxation factor.  Values > 1.0 accelerate
 *  convergence; typical range 1.1–1.5.  Too high causes oscillation. */
#define SOR_OMEGA 1.1f

/* ── Exponential map quaternion integration ───────────────────── */

/**
 * @brief Integrate orientation using the exponential map.
 *
 * Computes q_new = exp(0.5 * dω * dt) * q_old, which is the exact
 * rotation for a constant angular velocity increment dω over timestep dt.
 * Unlike the Euler update (q += 0.5*dt*Ω*q), this stays on SO(3) exactly
 * and does not inject energy through quaternion drift.
 *
 * @param q     Current orientation quaternion.
 * @param dw    Angular velocity increment (rad/s).
 * @param dt    Timestep (seconds).
 * @return      Updated orientation quaternion (normalized).
 */
static phys_quat_t quat_integrate_expmap(phys_quat_t q,
                                          phys_vec3_t dw,
                                          float dt)
{
    float wx = dw.x * dt;
    float wy = dw.y * dt;
    float wz = dw.z * dt;
    float theta = sqrtf(wx * wx + wy * wy + wz * wz);

    phys_quat_t dq;
    if (theta > 1e-8f) {
        float half_theta = 0.5f * theta;
        float s = sinf(half_theta) / theta;
        dq.w = cosf(half_theta);
        dq.x = s * wx;
        dq.y = s * wy;
        dq.z = s * wz;
    } else {
        /* Small angle: sin(θ/2)/θ ≈ 0.5 */
        dq.w = 1.0f;
        dq.x = 0.5f * wx;
        dq.y = 0.5f * wy;
        dq.z = 0.5f * wz;
    }

    return quat_normalize_safe(quat_mul(dq, q), 1e-12f);
}

/* ── Implicit gyroscopic torque correction ────────────────────── */

/**
 * @brief Apply implicit gyroscopic torque correction to angular velocity.
 *
 * The gyroscopic torque τ_gyro = ω × (I·ω) causes precession and
 * nutation.  An explicit treatment is unstable for fast-spinning bodies.
 * This applies the implicit correction by solving:
 *   (I + h·[ω×]·I) · ω_new = I · ω_old
 *
 * For diagonal inertia in body space, this is a 3x3 linear system
 * that we solve via Cramer's rule for efficiency.
 *
 * @param omega      Current angular velocity (world space, modified in place).
 * @param inv_I_diag Diagonal inverse inertia in body-local frame.
 * @param orient     Body orientation quaternion.
 * @param dt         Timestep (seconds).
 */
static void apply_gyroscopic_correction(phys_vec3_t *omega,
                                         phys_vec3_t inv_I_diag,
                                         phys_quat_t orient,
                                         float dt)
{
    /* Convert angular velocity to body-local frame. */
    phys_quat_t q_inv = quat_conjugate(orient);
    phys_vec3_t w_local = quat_rotate_vec3(q_inv, *omega);

    /* Body-space inertia (not inverse). */
    float Ix = (inv_I_diag.x > 1e-12f) ? (1.0f / inv_I_diag.x) : 0.0f;
    float Iy = (inv_I_diag.y > 1e-12f) ? (1.0f / inv_I_diag.y) : 0.0f;
    float Iz = (inv_I_diag.z > 1e-12f) ? (1.0f / inv_I_diag.z) : 0.0f;

    /* RHS = I · ω_local */
    float rhs_x = Ix * w_local.x;
    float rhs_y = Iy * w_local.y;
    float rhs_z = Iz * w_local.z;

    /* LHS matrix = I + h · [ω×] · I  (body-space, diagonal I).
     * [ω×]·I = [[0, -wz*Iy, wy*Iz],
     *           [wz*Ix, 0, -wx*Iz],
     *           [-wy*Ix, wx*Iy, 0]]
     * So LHS = I + h * [ω×]·I:
     *   [[Ix,           -h*wz*Iy,      h*wy*Iz],
     *    [h*wz*Ix,       Iy,           -h*wx*Iz],
     *    [-h*wy*Ix,      h*wx*Iy,       Iz     ]] */
    float a00 = Ix;
    float a01 = -dt * w_local.z * Iy;
    float a02 =  dt * w_local.y * Iz;
    float a10 =  dt * w_local.z * Ix;
    float a11 = Iy;
    float a12 = -dt * w_local.x * Iz;
    float a20 = -dt * w_local.y * Ix;
    float a21 =  dt * w_local.x * Iy;
    float a22 = Iz;

    /* Solve via Cramer's rule. */
    float det = a00 * (a11 * a22 - a12 * a21)
              - a01 * (a10 * a22 - a12 * a20)
              + a02 * (a10 * a21 - a11 * a20);

    if (fabsf(det) < 1e-20f) return;

    float inv_det = 1.0f / det;
    phys_vec3_t w_new_local;
    w_new_local.x = inv_det * (rhs_x * (a11 * a22 - a12 * a21)
                              - a01  * (rhs_y * a22 - a12 * rhs_z)
                              + a02  * (rhs_y * a21 - a11 * rhs_z));
    w_new_local.y = inv_det * (a00 * (rhs_y * a22 - a12 * rhs_z)
                              - rhs_x * (a10 * a22 - a12 * a20)
                              + a02   * (a10 * rhs_z - rhs_y * a20));
    w_new_local.z = inv_det * (a00 * (a11 * rhs_z - rhs_y * a21)
                              - a01 * (a10 * rhs_z - rhs_y * a20)
                              + rhs_x * (a10 * a21 - a11 * a20));

    /* Convert back to world frame. */
    *omega = quat_rotate_vec3(orient, w_new_local);
}

/* ── Internal: compute per-island iteration count ─────────────── */

/**
 * @brief Compute a per-island solver iteration count scaled by the
 *        maximum body speed in the island.
 *
 * Islands where every body is slow use the base iteration count.
 * Islands with fast-moving bodies get up to ADAPTIVE_ITER_MULT × base
 * iterations, linearly interpolated between ADAPTIVE_SPEED_LOW and
 * ADAPTIVE_SPEED_HIGH.
 *
 * @param island       The island to inspect.
 * @param bodies       Body array (read-only).
 * @param velocities   Solver velocity workspace (post-gravity).
 * @param base_iters   Configured base iteration count.
 * @return Iteration count for this island (>= base_iters).
 */
static uint32_t compute_island_iterations(
    const phys_island_t *island,
    const phys_body_t *bodies,
    const phys_velocity_t *velocities,
    uint32_t base_iters)
{
    float max_speed_sq = 0.0f;
    for (uint32_t bi = 0; bi < island->body_count; bi++) {
        uint32_t idx = island->body_indices[bi];
        if (bodies[idx].inv_mass == 0.0f) continue; /* skip static */
        phys_vec3_t v = velocities[idx].linear;
        float speed_sq = vec3_dot(v, v);
        if (speed_sq > max_speed_sq) {
            max_speed_sq = speed_sq;
        }
    }

    const float lo2 = ADAPTIVE_SPEED_LOW  * ADAPTIVE_SPEED_LOW;
    const float hi2 = ADAPTIVE_SPEED_HIGH * ADAPTIVE_SPEED_HIGH;

    if (max_speed_sq <= lo2) {
        /* Even at low speed, serial chains need enough iterations for
         * impulse to propagate root-to-tip.  Floor at the island body
         * count so a 40-body chain gets at least 40 iterations. */
        uint32_t chain_floor = island->body_count;
        return (chain_floor > base_iters) ? chain_floor : base_iters;
    }

    /* Scale down the adaptive multiplier for large islands to bound
     * worst-case cost (iterations × constraints).  Small islands
     * are cheap regardless, so let them use the full multiplier. */
    uint32_t mult = ADAPTIVE_ITER_MULT;
    if (island->constraint_count > 512) {
        mult = 2;
    } else if (island->constraint_count > 256) {
        mult = (mult > 3) ? 3 : mult;
    }

    if (max_speed_sq >= hi2) {
        return base_iters * mult;
    }

    /* Sqrt ramp: aggressive at moderate speeds, plateaus at extremes. */
    float t = (max_speed_sq - lo2) / (hi2 - lo2);
    t = sqrtf(t);
    uint32_t extra = (uint32_t)(t * (float)(base_iters * (mult - 1)));
    uint32_t result = base_iters + extra;

    /* Floor: serial chains need body_count iterations minimum. */
    uint32_t chain_floor = island->body_count;
    return (result > chain_floor) ? result : chain_floor;
}

static bool island_routes_xpbd_(const phys_island_t *island,
                                const phys_constraint_t *constraints,
                                const phys_body_t *bodies)
{
    if (!island || !bodies) {
        return false;
    }

    /* TIER_ANIM islands are NOT routed to XPBD.  They use the TGS
     * coupled implicit solver (solve_joint_coupled) with XPBD-style
     * compliance/damping regularization in the velocity equation.
     * Only check constraint solver_mode, not body tier. */

    if (!constraints) {
        return false;
    }

    for (uint32_t ci = 0; ci < island->constraint_count; ++ci) {
        if (constraints[island->constraint_indices[ci]].solver_mode ==
            PHYS_SOLVER_XPBD) {
            return true;
        }
    }

    return false;
}

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
            !phys_body_is_sleeping(&args->bodies[i]) &&
            !(args->bodies[i].flags & PHYS_BODY_FLAG_NO_GRAVITY)) {
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
                       const phys_mat3_t *inv_i_a,
                       float inv_mass_b,
                       const phys_mat3_t *inv_i_b)
{
    /* Compute J·v (relative velocity along the constraint direction). */
    float jv = vec3_dot(row->J_va, va->linear)
             + vec3_dot(row->J_wa, va->angular)
             + vec3_dot(row->J_vb, vb->linear)
             + vec3_dot(row->J_wb, vb->angular);

    /* Viscous damping: scale velocity error by (1 + d) so the
     * solver applies a stronger correction that opposes relative motion.
     * Dimensionless: d=1 doubles the effective velocity correction,
     * d=0.5 increases it by 50%.  Timestep-independent. */
    float jv_damped = jv * (1.0f + row->damping);
    float delta_lambda = (row->bias - jv_damped) * row->effective_mass;

    /* Successive over-relaxation: scale impulse to accelerate convergence. */
    delta_lambda *= SOR_OMEGA;

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

    /* Apply angular velocity corrections (world-space inverse inertia). */
    phys_vec3_t ang_impulse_a = phys_mat3_mul_vec3(inv_i_a, row->J_wa);
    va->angular = vec3_add(va->angular,
                           vec3_scale(ang_impulse_a, delta_lambda));

    phys_vec3_t ang_impulse_b = phys_mat3_mul_vec3(inv_i_b, row->J_wb);
    vb->angular = vec3_add(vb->angular,
                           vec3_scale(ang_impulse_b, delta_lambda));
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
                                const phys_mat3_t *inv_i_a,
                                float inv_mass_b,
                                const phys_mat3_t *inv_i_b)
{
    float excess = penetration - slop;
    if (excess < SPLIT_MIN_PHI) { return; }

    /* Position correction bias: target pseudo-velocity to resolve
     * a fraction (ERP) of the penetration excess per substep. */
    float pos_bias = excess * inv_dt * SPLIT_ERP;

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

    phys_vec3_t ang_a = phys_mat3_mul_vec3(inv_i_a, row->J_wa);
    pva->angular = vec3_add(pva->angular, vec3_scale(ang_a, delta_lambda));

    phys_vec3_t ang_b = phys_mat3_mul_vec3(inv_i_b, row->J_wb);
    pvb->angular = vec3_add(pvb->angular, vec3_scale(ang_b, delta_lambda));
}

/* ── Solve joint split-impulse position correction row ────────────── */

/**
 * @brief Solve one joint row's position error via split impulse.
 *
 * Joint rows store the raw position error (meters) in the bias field.
 * Unlike contacts (unilateral, clamp ≥ 0), joints use bilateral
 * clamping so the pseudo-impulse can push or pull.
 *
 * @param row         Jacobian row (pseudo_lambda is updated).
 * @param pva         Pseudo-velocity for body A.
 * @param pvb         Pseudo-velocity for body B.
 * @param inv_dt      1 / dt.
 * @param inv_mass_a  Inverse mass of body A.
 * @param inv_i_a     Diagonal inverse inertia of body A.
 * @param inv_mass_b  Inverse mass of body B.
 * @param inv_i_b     Diagonal inverse inertia of body B.
 */
static void solve_joint_position_row(phys_jacobian_row_t *row,
                                      phys_velocity_t *pva,
                                      phys_velocity_t *pvb,
                                      float inv_dt,
                                      float inv_mass_a,
                                      const phys_mat3_t *inv_i_a,
                                      float inv_mass_b,
                                      const phys_mat3_t *inv_i_b)
{
    /* row->bias holds the raw position error (meters, signed). */
    float error = row->bias;
    if (fabsf(error) < 1e-7f) { return; }

    /* Target pseudo-velocity to correct the error in one substep.
     * Negative sign: positive error means anchors are separated, so
     * correction drives the relative anchor velocity negative.
     *
     * Joint positional rows use moderate ERP to avoid Gauss-Seidel
     * divergence in long joint chains (ragdoll: 19 joints).
     * Pseudo-velocities are scaled by tier_substeps/max_substeps
     * before integration, so effective inv_dt matches body_dt.
     * Angular rows use softer ERP to prevent fighting. */
    float erp = (row->flags & PHYS_ROW_FLAG_ANGULAR)
              ? 0.05f
              : 0.4f;
    float pos_bias = -error * inv_dt * erp;

    /* Current pseudo-velocity along constraint direction. */
    float jv = vec3_dot(row->J_va, pva->linear)
             + vec3_dot(row->J_wa, pva->angular)
             + vec3_dot(row->J_vb, pvb->linear)
             + vec3_dot(row->J_wb, pvb->angular);

    float delta_lambda = (pos_bias - jv) * row->effective_mass;

    /* Bilateral clamp (joints can push and pull). */
    float old_lambda = row->pseudo_lambda;
    row->pseudo_lambda = old_lambda + delta_lambda;
    delta_lambda = row->pseudo_lambda - old_lambda;

    if (fabsf(delta_lambda) < 1e-10f) { return; }

    /* Apply pseudo-velocity corrections. */
    pva->linear = vec3_add(pva->linear,
                           vec3_scale(row->J_va, inv_mass_a * delta_lambda));
    pvb->linear = vec3_add(pvb->linear,
                           vec3_scale(row->J_vb, inv_mass_b * delta_lambda));

    phys_vec3_t ang_a = phys_mat3_mul_vec3(inv_i_a, row->J_wa);
    pva->angular = vec3_add(pva->angular, vec3_scale(ang_a, delta_lambda));

    phys_vec3_t ang_b = phys_mat3_mul_vec3(inv_i_b, row->J_wb);
    pvb->angular = vec3_add(pvb->angular, vec3_scale(ang_b, delta_lambda));
}

/* ── Nonlinear joint position projection ──────────────────────────── */

/** Minimum anchor error (meters) to trigger nonlinear projection. */
#define NL_PROJ_MIN_ERROR 0.01f

/** Number of nonlinear projection passes after TGS iterations. */
#define NL_PROJ_PASSES 4

/** Fraction of error corrected per nonlinear projection pass. */
#define NL_PROJ_FRACTION 0.8f

/**
 * @brief Integrate a quaternion by an angular velocity over dt.
 *
 * Uses the standard quaternion derivative: dq = 0.5 * omega_q * q.
 * The result is normalized.
 */
static phys_quat_t tgs_quat_integrate(phys_quat_t q, phys_vec3_t w,
                                       float dt) {
    phys_quat_t omega_q = { w.x, w.y, w.z, 0.0f };
    phys_quat_t dq = quat_mul(omega_q, q);
    float half_dt = 0.5f * dt;
    phys_quat_t result = {
        q.x + dq.x * half_dt,
        q.y + dq.y * half_dt,
        q.z + dq.z * half_dt,
        q.w + dq.w * half_dt,
    };
    return quat_normalize_safe(result, 1e-8f);
}

/**
 * @brief Nonlinear position projection for joints after TGS iterations.
 *
 * After the iterative velocity solve, pseudo-velocities encode a linear
 * approximation of position correction.  For joints with large errors,
 * this linearization is inaccurate because the Jacobians were computed
 * at the start of the substep and don't account for how rotation changes
 * the lever arm direction.
 *
 * This function does extra passes that:
 *   1. Predict each body's state after integrating pseudo-velocity.
 *   2. Recompute world anchors from that predicted state.
 *   3. Measure the actual residual anchor error.
 *   4. Apply a correction to pseudo-velocities that accounts for both
 *      translation and the angular swing needed to redirect the lever arm.
 *
 * This is essentially TGS position-level solve with nonlinear re-
 * linearization — the key insight being that we recompute the constraint
 * direction each pass instead of using stale Jacobians.
 *
 * @param joints       Joint array.
 * @param joint_count  Number of joints.
 * @param bodies       Body array (read-only positions/orientations).
 * @param pseudo       Pseudo-velocity workspace (modified in-place).
 * @param body_count   Number of bodies.
 * @param dt           Substep timestep.
 */
static void project_joints_nonlinear(const phys_joint_t *joints,
                                      uint32_t joint_count,
                                      const struct phys_body *bodies,
                                      phys_velocity_t *pseudo,
                                      uint32_t body_count,
                                      float dt)
{
    if (!joints || joint_count == 0 || !pseudo || dt <= 0.0f) return;

    const float inv_dt = 1.0f / dt;

    for (uint32_t pass = 0; pass < NL_PROJ_PASSES; pass++) {
        for (uint32_t ji = 0; ji < joint_count; ji++) {
            const phys_joint_t *j = &joints[ji];
            if (j->body_a >= body_count || j->body_b >= body_count) continue;

            const phys_body_t *ba = &bodies[j->body_a];
            const phys_body_t *bb = &bodies[j->body_b];

            /* Predict body state after integrating pseudo-velocities. */
            phys_vec3_t pos_a = vec3_add(ba->position,
                vec3_scale(pseudo[j->body_a].linear, dt));
            phys_vec3_t pos_b = vec3_add(bb->position,
                vec3_scale(pseudo[j->body_b].linear, dt));

            phys_quat_t ori_a = tgs_quat_integrate(
                ba->orientation, pseudo[j->body_a].angular, dt);
            phys_quat_t ori_b = tgs_quat_integrate(
                bb->orientation, pseudo[j->body_b].angular, dt);

            /* Compute world anchors from predicted state. */
            phys_vec3_t rA = quat_rotate_vec3(ori_a, j->local_anchor_a);
            phys_vec3_t rB = quat_rotate_vec3(ori_b, j->local_anchor_b);
            phys_vec3_t wa = vec3_add(pos_a, rA);
            phys_vec3_t wb = vec3_add(pos_b, rB);

            /* Compute anchor error vector: wb - wa. */
            phys_vec3_t error = vec3_sub(wb, wa);

            /* For distance joints, error is along the separation axis
             * with magnitude = current_dist - rest_length. */
            float target = 0.0f;
            if (j->type == PHYS_JOINT_DISTANCE) {
                target = j->rest_length;
                float dist = vec3_magnitude(error);
                if (dist < 1e-7f) continue;
                /* Scalar error: how far off from rest length. */
                float scalar_err = dist - target;
                if (fabsf(scalar_err) < NL_PROJ_MIN_ERROR) continue;
                /* Direction from A to B. */
                phys_vec3_t dir = vec3_scale(error, 1.0f / dist);
                error = vec3_scale(dir, scalar_err);
            } else {
                /* Ball/hinge: target is zero separation. */
                float err_mag = vec3_magnitude(error);
                if (err_mag < NL_PROJ_MIN_ERROR) continue;
            }

            /* Inverse masses. */
            float im_a = ba->inv_mass;
            float im_b = bb->inv_mass;
            float im_total = im_a + im_b;
            if (im_total < 1e-12f) continue;

            /* --- Linear correction --- */
            /* Distribute error correction weighted by inverse mass.
             * Convert position correction to pseudo-velocity: dp/dt. */
            float frac_a = im_a / im_total;
            float frac_b = im_b / im_total;
            phys_vec3_t correction = vec3_scale(error, NL_PROJ_FRACTION);

            if (im_a > 0.0f) {
                pseudo[j->body_a].linear = vec3_add(
                    pseudo[j->body_a].linear,
                    vec3_scale(correction, frac_a * inv_dt));
            }
            if (im_b > 0.0f) {
                pseudo[j->body_b].linear = vec3_sub(
                    pseudo[j->body_b].linear,
                    vec3_scale(correction, frac_b * inv_dt));
            }

            /* --- Angular correction --- */
            /* For each body with a nonzero lever arm, compute the angular
             * impulse needed to swing the lever arm toward the target.
             *
             * The idea: if the world anchor is off by 'e', and the lever
             * arm is 'r', then a small rotation 'dtheta' about axis
             * (r × e) / |r × e| would move the anchor by |dtheta| * |r|
             * in the direction of e (projected perpendicular to r).
             *
             * delta_omega = (r × e) / |r|^2, scaled by fraction and inv_dt. */
            float rA_len2 = vec3_dot(rA, rA);
            if (im_a > 0.0f && rA_len2 > 1e-6f) {
                /* r × error gives rotation axis × angle (small-angle). */
                phys_vec3_t r_cross_e = vec3_cross(rA, correction);
                phys_vec3_t ang_corr = vec3_scale(r_cross_e,
                    frac_a * inv_dt / rA_len2);
                pseudo[j->body_a].angular = vec3_add(
                    pseudo[j->body_a].angular, ang_corr);
            }

            float rB_len2 = vec3_dot(rB, rB);
            if (im_b > 0.0f && rB_len2 > 1e-6f) {
                phys_vec3_t r_cross_e = vec3_cross(rB, correction);
                /* Body B correction is opposite: we want B's anchor to
                 * move toward A's, so subtract. */
                phys_vec3_t ang_corr = vec3_scale(r_cross_e,
                    -frac_b * inv_dt / rB_len2);
                pseudo[j->body_b].angular = vec3_add(
                    pseudo[j->body_b].angular, ang_corr);
            }
        }
    }
}

/* ── Coupled implicit Gauss-Seidel for joint constraints ──────────── */

/**
 * @brief Solve a joint constraint using the coupled implicit method.
 *
 * Uses the XPBD-regularized coupled update equation:
 *
 *   Δλ = -(J·v + C/h + (α/h)·λ_total) / (J·M⁻¹·J^T + α/h² + γ/h)
 *
 * Then applies coupled velocity + position update:
 *   v ← v + M⁻¹·J^T·Δλ
 *   p ← p + h·(M⁻¹·J^T·Δλ)
 *   λ_total ← λ_total + Δλ
 *
 * This eliminates the split between velocity solve and position
 * correction that causes energy injection in long joint chains.
 *
 * @param c              Joint constraint to solve.
 * @param velocities     Solver velocity workspace (modified).
 * @param bodies_mut     Mutable body array (position/orientation updated).
 * @param inv_inertia_world  Precomputed world-space inverse inertia (may be NULL).
 * @param dt             Substep timestep.
 * @param inv_dt         1 / dt.
 */
static void solve_joint_coupled(phys_constraint_t *c,
                                 phys_velocity_t *velocities,
                                 phys_body_t *bodies_mut,
                                 const phys_mat3_t *inv_inertia_world,
                                 float dt,
                                 float inv_dt)
{
    phys_velocity_t *va = &velocities[c->body_a];
    phys_velocity_t *vb = &velocities[c->body_b];

    float inv_mass_a = bodies_mut[c->body_a].inv_mass;
    float inv_mass_b = bodies_mut[c->body_b].inv_mass;

    phys_mat3_t fallback_a, fallback_b;
    const phys_mat3_t *inv_i_a;
    const phys_mat3_t *inv_i_b;
    if (inv_inertia_world) {
        inv_i_a = &inv_inertia_world[c->body_a];
        inv_i_b = &inv_inertia_world[c->body_b];
    } else {
        fallback_a = phys_mat3_inv_inertia_world(
            bodies_mut[c->body_a].orientation,
            bodies_mut[c->body_a].inv_inertia_diag);
        fallback_b = phys_mat3_inv_inertia_world(
            bodies_mut[c->body_b].orientation,
            bodies_mut[c->body_b].inv_inertia_diag);
        inv_i_a = &fallback_a;
        inv_i_b = &fallback_b;
    }

    /* Compliance α and damping γ from constraint.
     * Drive rows use drive_compliance, angular limit rows use
     * angular_compliance (0 = fall back to positional compliance). */
    float alpha_hard    = c->compliance;
    float alpha_angular = c->angular_compliance;
    float alpha_drive   = c->drive_compliance;
    float damping_ratio = c->joint_damping;

    for (uint8_t r = 0; r < c->row_count; r++) {
        phys_jacobian_row_t *row = &c->rows[r];

        /* Compute J·v (relative velocity along constraint direction). */
        float jv = vec3_dot(row->J_va, va->linear)
                 + vec3_dot(row->J_wa, va->angular)
                 + vec3_dot(row->J_vb, vb->linear)
                 + vec3_dot(row->J_wb, vb->angular);

        /* row->bias holds the position-level constraint error C_i
         * (set by joint builders: anchor separation in meters for
         * positional rows, angle error in radians for angular rows).
         *
         * Coupled bias: -(J·v + β·C/h + (α/h)·λ_total)
         * row->lambda serves as λ_total (accumulated across iterations).
         *
         * β (position ERP) controls how aggressively position errors
         * are corrected per substep.  For the coupled solver, we use
         * a fraction rather than full correction to prevent oscillation
         * when multiple constraints compete. */
        float C_i = row->bias;
        /* Position ERP for coupled solver.  Angular limit rows use a
         * much lower ERP to avoid energy injection from overcorrection
         * while still gently enforcing limits. */
        const float coupled_erp = (row->flags & PHYS_ROW_FLAG_ANGULAR)
                                ? 0.1f : 0.6f;

        /* Select compliance per row type:
         *   drive rows  → drive_compliance
         *   angular rows → angular_compliance (falls back to hard)
         *   positional   → compliance */
        float alpha;
        if (row->flags & PHYS_ROW_FLAG_DRIVE) {
            alpha = alpha_drive;
        } else if ((row->flags & PHYS_ROW_FLAG_ANGULAR) && alpha_angular > 0.0f) {
            alpha = alpha_angular;
        } else {
            alpha = alpha_hard;
        }

        float alpha_over_h2 = alpha * inv_dt * inv_dt;

        float numerator = -(jv + coupled_erp * C_i * inv_dt
                            + alpha * inv_dt * row->lambda);

        /* Regularized effective mass denominator:
         *   J·M⁻¹·J^T + h²·K_geo + α/h² + γ/h
         * row->effective_mass = 1/(J·M⁻¹·J^T), so we need the raw
         * J·M⁻¹·J^T = 1/effective_mass. */
        float jmjt = (row->effective_mass > 1e-12f)
                    ? (1.0f / row->effective_mass)
                    : 1e12f;

        /* Geometric stiffness correction for angular rows.
         *
         * The geometric stiffness matrix K_geo accounts for the change
         * in constraint force direction as bodies rotate.  Without it,
         * the solver overshoots the spherical constraint manifold,
         * injecting radial energy.
         *
         * For orthogonal angular axes (cone-twist joint frame), the
         * projected K_geo for row i simplifies to:
         *   k_geo_i = -Σ_{j≠i} λ_j * ((n_j · n_i)² - 1) = Σ_{j≠i} |λ_j|
         *
         * Added to denominator as h² · k_geo to stabilize the solve. */
        float k_geo = 0.0f;
        if (row->flags & PHYS_ROW_FLAG_ANGULAR) {
            for (uint8_t s = 0; s < c->row_count; s++) {
                if (s == r) continue;
                if (!(c->rows[s].flags & PHYS_ROW_FLAG_ANGULAR)) continue;
                /* Use dot product between angular Jacobian axes to handle
                 * non-orthogonal cases correctly.  For J_wb (body B axis):
                 *   n_i · n_j gives the alignment between axes. */
                float dot = vec3_dot(row->J_wb, c->rows[s].J_wb);
                float contrib = dot * dot - 1.0f;  /* ≤ 0 for unit axes */
                k_geo += fabsf(c->rows[s].lambda) * (-contrib);
            }
        }

        /* Damping: γ/h = β·α/h² where β is the dimensionless damping
         * ratio from the joint.  This makes damping timestep-independent,
         * scaling with the compliance regularization. */
        float gamma_over_h = damping_ratio * alpha_over_h2;

        float denom = jmjt + dt * dt * k_geo + alpha_over_h2 + gamma_over_h;
        float inv_denom = (denom > 1e-12f) ? (1.0f / denom) : 0.0f;

        float delta_lambda = numerator * inv_denom;

        /* Clamp accumulated impulse within bounds. */
        float old_lambda = row->lambda;
        row->lambda = old_lambda + delta_lambda;
        if (row->lambda < row->lambda_min) row->lambda = row->lambda_min;
        if (row->lambda > row->lambda_max) row->lambda = row->lambda_max;
        delta_lambda = row->lambda - old_lambda;

        if (fabsf(delta_lambda) < 1e-12f) continue;

        /* Compute velocity deltas: Δv = M⁻¹·J^T·Δλ */
        phys_vec3_t dv_lin_a = vec3_scale(row->J_va,
                                           inv_mass_a * delta_lambda);
        phys_vec3_t dv_lin_b = vec3_scale(row->J_vb,
                                           inv_mass_b * delta_lambda);

        phys_vec3_t dv_ang_a = vec3_scale(
            phys_mat3_mul_vec3(inv_i_a, row->J_wa), delta_lambda);
        phys_vec3_t dv_ang_b = vec3_scale(
            phys_mat3_mul_vec3(inv_i_b, row->J_wb), delta_lambda);

        /* Update velocities. */
        va->linear  = vec3_add(va->linear,  dv_lin_a);
        va->angular = vec3_add(va->angular, dv_ang_a);
        vb->linear  = vec3_add(vb->linear,  dv_lin_b);
        vb->angular = vec3_add(vb->angular, dv_ang_b);

        /* Coupled position update: accumulate position changes inline
         * so that inter-iteration Jacobian rebuilds use up-to-date
         * body positions and world-space anchors.  This is the core
         * of the nonlinear implicit GS method.
         *
         * The caller saves original positions, lets these accumulate,
         * then converts the total correction into pseudo-velocities
         * for the integrator to apply alongside v*dt. */
        phys_vec3_t dp_a = vec3_scale(dv_lin_a, dt);
        phys_vec3_t dp_b = vec3_scale(dv_lin_b, dt);
        bodies_mut[c->body_a].position =
            vec3_add(bodies_mut[c->body_a].position, dp_a);
        bodies_mut[c->body_b].position =
            vec3_add(bodies_mut[c->body_b].position, dp_b);

        /* Integrate orientation using exponential map (symplectic).
         * exp(0.5·dω·dt) * q stays on SO(3) exactly, unlike the Euler
         * update q += 0.5·dt·Ω·q which drifts off the unit quaternion
         * manifold and injects energy through normalization correction. */
        bodies_mut[c->body_a].orientation = quat_integrate_expmap(
            bodies_mut[c->body_a].orientation, dv_ang_a, dt);
        bodies_mut[c->body_b].orientation = quat_integrate_expmap(
            bodies_mut[c->body_b].orientation, dv_ang_b, dt);
    }
}

/**
 * @brief Solve all rows of a single constraint: normal + friction + split.
 *
 * For joint constraints when bodies_mut is non-NULL, uses the coupled
 * implicit solver (no split-impulse, position updated inline).
 * Otherwise uses the standard TGS velocity solve + split-impulse.
 */
static void solve_one_constraint(phys_constraint_t *c,
                                  phys_velocity_t *velocities,
                                  phys_velocity_t *pseudo,
                                  const struct phys_body *bodies,
                                  const phys_mat3_t *inv_inertia_world,
                                  float slop,
                                  float inv_dt,
                                  float tick_dt,
                                  const uint32_t *tier_substep_counts,
                                  phys_body_t *bodies_mut)
{
    phys_velocity_t *va = &velocities[c->body_a];
    phys_velocity_t *vb = &velocities[c->body_b];

    float inv_mass_a = bodies[c->body_a].inv_mass;
    float inv_mass_b = bodies[c->body_b].inv_mass;

    /* Use precomputed world-space inertia if available, else compute inline. */
    phys_mat3_t fallback_a, fallback_b;
    const phys_mat3_t *inv_i_a;
    const phys_mat3_t *inv_i_b;
    if (inv_inertia_world) {
        inv_i_a = &inv_inertia_world[c->body_a];
        inv_i_b = &inv_inertia_world[c->body_b];
    } else {
        fallback_a = phys_mat3_inv_inertia_world(
            bodies[c->body_a].orientation, bodies[c->body_a].inv_inertia_diag);
        fallback_b = phys_mat3_inv_inertia_world(
            bodies[c->body_b].orientation, bodies[c->body_b].inv_inertia_diag);
        inv_i_a = &fallback_a;
        inv_i_b = &fallback_b;
    }

    if (c->is_joint) {
        /* Coupled implicit solver: when bodies_mut is available, use the
         * coupled update that modifies position+velocity together.
         * This eliminates the split-impulse energy injection problem. */
        if (bodies_mut) {
            float dt = (inv_dt > 0.0f) ? (1.0f / inv_dt) : 0.0f;
            solve_joint_coupled(c, velocities, bodies_mut,
                                inv_inertia_world, dt, inv_dt);
            return;
        }

        /* Fallback: standard TGS velocity-level solve with Baumgarte
         * leak + split-impulse position correction.  Used when
         * bodies_mut is NULL (non-TIER_ANIM islands). */

        /* Joint constraints: velocity-level solve with a small Baumgarte
         * leak proportional to body speed, then split-impulse position
         * correction using the position error stored in each row's bias.
         *
         * At low speeds, bias → 0 (pure split-impulse).  At high speeds,
         * leak a fraction of -error*inv_dt into the velocity bias so the
         * solver actively steers anchors together during velocity solve,
         * preventing drift that split-impulse alone can't catch. */

        /* Compute max body speed for Baumgarte leak scaling. */
        phys_vec3_t vel_a = va->linear;
        phys_vec3_t vel_b = vb->linear;
        float spd_a = vec3_dot(vel_a, vel_a);
        float spd_b = vec3_dot(vel_b, vel_b);
        float max_spd2 = spd_a > spd_b ? spd_a : spd_b;

        /* Baumgarte fraction: 0 below 5 m/s, ramps to 0.6 at 60 m/s. */
        const float baumgarte_lo2 = 5.0f * 5.0f;      /* 25 */
        const float baumgarte_hi2 = 60.0f * 60.0f;     /* 3600 */
        const float baumgarte_max = 0.6f;
        float baumgarte = 0.0f;
        if (max_spd2 > baumgarte_lo2) {
            float t = (max_spd2 - baumgarte_lo2)
                    / (baumgarte_hi2 - baumgarte_lo2);
            if (t > 1.0f) { t = 1.0f; }
            baumgarte = baumgarte_max * t;
        }

        /* Save bias values (position errors), set velocity-level bias
         * to Baumgarte leak: -error * inv_dt * baumgarte_fraction.
         * Angular rows (PHYS_ROW_FLAG_ANGULAR) get a much lower
         * baumgarte to prevent angular corrections from destabilizing
         * positional rows via coupled anchor movement. */
        float saved_bias[PHYS_MAX_CONSTRAINT_ROWS];
        float saved_eff_mass[PHYS_MAX_CONSTRAINT_ROWS];
        /* Animation constraints (is_joint == 1) use softened effective
         * mass so structural joints and contacts override them. */
        const float anim_softness = (c->is_joint == 1) ? 0.5f : 1.0f;

        for (uint8_t r = 0; r < c->row_count; r++) {
            saved_bias[r] = c->rows[r].bias;
            saved_eff_mass[r] = c->rows[r].effective_mass;
            c->rows[r].effective_mass *= anim_softness;
            float row_baumgarte = (c->rows[r].flags & PHYS_ROW_FLAG_ANGULAR)
                                ? baumgarte * 0.05f
                                : baumgarte;
            c->rows[r].bias = -saved_bias[r] * inv_dt * row_baumgarte;
        }

        /* Velocity-level solve: all rows with bilateral bounds. */
        for (uint8_t r = 0; r < c->row_count; r++) {
            solve_row(&c->rows[r], va, vb,
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }

        /* Restore bias and effective mass for position correction. */
        for (uint8_t r = 0; r < c->row_count; r++) {
            c->rows[r].bias = saved_bias[r];
            c->rows[r].effective_mass = saved_eff_mass[r];
        }
        if (pseudo) {
            /* Compliance softening for position correction only.
             * In split-impulse TGS, compliance reduces the strength of
             * positional drift correction (elastic response) without
             * weakening the velocity-level constraint solve.
             *   m_soft = m_eff / (1 + α * m_eff * inv_dt²)
             * Use per-body effective inv_dt that accounts for tiered
             * substep scaling (pseudo_velocities are scaled by
             * tier_substeps/max_substeps before integration). */
            float eff_inv_dt = inv_dt;
            if (tier_substep_counts && tick_dt > 0.0f) {
                uint8_t t_a = bodies[c->body_a].tier;
                uint8_t t_b = bodies[c->body_b].tier;
                uint32_t ts_a = tier_substep_counts[t_a];
                uint32_t ts_b = tier_substep_counts[t_b];
                uint32_t ts = (ts_a < ts_b) ? ts_a : ts_b;
                if (ts == 0) { ts = 1; }
                eff_inv_dt = (float)ts / tick_dt;
            }
            const float comp_hard  = c->compliance * eff_inv_dt * eff_inv_dt;
            const float comp_ang   = c->angular_compliance * eff_inv_dt * eff_inv_dt;
            const float comp_drive = c->drive_compliance * eff_inv_dt * eff_inv_dt;
            for (uint8_t r = 0; r < c->row_count; r++) {
                float compliance_factor;
                if (c->rows[r].flags & PHYS_ROW_FLAG_DRIVE) {
                    compliance_factor = comp_drive;
                } else if ((c->rows[r].flags & PHYS_ROW_FLAG_ANGULAR) && comp_ang > 0.0f) {
                    compliance_factor = comp_ang;
                } else {
                    compliance_factor = comp_hard;
                }
                float m_save = c->rows[r].effective_mass;
                if (compliance_factor > 0.0f) {
                    float m = m_save / (1.0f + compliance_factor * m_save);
                    c->rows[r].effective_mass = m;
                }
                solve_joint_position_row(
                    &c->rows[r],
                    &pseudo[c->body_a], &pseudo[c->body_b],
                    inv_dt,
                    inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
                c->rows[r].effective_mass = m_save;
            }
        }
        return;
    }

    /* Solve normal row first (row 0). */
    solve_row(&c->rows[0], va, vb,
              inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);

    {
        /* Split impulse: position correction into pseudo-velocities. */
        if (pseudo) {
            solve_position_row(
                &c->rows[0],
                &pseudo[c->body_a], &pseudo[c->body_b],
                c->penetration, slop, inv_dt,
                inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }

        /* Coulomb friction cone. */
        float friction_limit = c->friction * c->rows[0].lambda;
        for (uint8_t r = 1; r < c->row_count; r++) {
            c->rows[r].lambda_min = -friction_limit;
            c->rows[r].lambda_max =  friction_limit;
            solve_row(&c->rows[r], va, vb,
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }
    }
}

/* ── Colored island solve ────────────────────────────────────────── */

/**
 * @brief Solve an island using graph coloring for constraint ordering.
 *
 * For each solver iteration, constraints are processed color-by-color.
 * All constraints within the same color batch are independent (share no
 * bodies) and thus the ordering within a batch does not matter.  This
 * enables future parallelization: same-color constraints can be solved
 * by different threads without synchronization.
 *
 * Currently the per-color batch is solved sequentially, but the
 * coloring structure is in place for parallel dispatch.
 *
 * @return true if coloring succeeded, false to fall back to sequential.
 */
static bool solve_island_colored(const phys_island_t *island,
                                  const phys_tgs_solve_args_t *args,
                                  uint32_t iters,
                                  float slop,
                                  float inv_dt)
{
    /* Build a temporary constraint array pointing to this island's
     * constraints so that the coloring algorithm can index them
     * contiguously (0..island->constraint_count-1). */
    phys_frame_arena_t *arena = args->frame_arena;

    /* We need the coloring to reference body indices from the island's
     * constraints.  phys_constraint_color works on a contiguous constraint
     * array, so we pass the full world constraint array and use the island's
     * constraint_indices to build a local contiguous copy. */
    uint32_t n = island->constraint_count;
    phys_constraint_t *local = phys_frame_arena_alloc(
        arena, n * sizeof(phys_constraint_t), _Alignof(phys_constraint_t));
    if (!local) { return false; }

    /* Copy island constraints into contiguous local array (only body_a/body_b
     * are needed for coloring, but we copy the full struct for later solve). */
    for (uint32_t ci = 0; ci < n; ++ci) {
        local[ci] = args->constraints[island->constraint_indices[ci]];
    }

    phys_color_result_t coloring;
    int rc = phys_constraint_color(local, n, args->body_count, arena, &coloring);
    if (rc != 0) { return false; }

    /* Interleaved two-pass solve: within each iteration, animation
     * constraints (is_joint == 1) are solved first so structural joints
     * and contacts (is_joint == 0 or 2) can override them.  Animation
     * constraints use softened effective mass (50%) so they yield to
     * structural constraints when they conflict. */
    phys_velocity_t *pseudo = args->pseudo_velocities;

    for (uint32_t iter = 0; iter < iters; ++iter) {
        /* All constraints in a single pass per color. */
        for (uint32_t color = 0; color < coloring.num_colors; ++color) {
            for (uint32_t ci = 0; ci < n; ++ci) {
                if (coloring.colors[ci] != color) { continue; }
                uint32_t c_idx = island->constraint_indices[ci];
                solve_one_constraint(&args->constraints[c_idx],
                                     args->velocities, pseudo,
                                     args->bodies, args->inv_inertia_world,
                                     slop, inv_dt,
                                     args->tick_dt,
                                     args->tier_substep_counts,
                                     args->bodies_mut);
            }
        }
    }

    return true;
}

/* ── Coupled island helpers ───────────────────────────────────────── */

/**
 * @brief Check if an island uses the coupled implicit solver.
 *
 * Returns true when bodies_mut is available and any dynamic body in
 * the island has TIER_ANIM.  The coupled solver updates positions
 * inline and rebuilds Jacobians between iterations.
 */
static bool island_is_coupled_(const phys_island_t *island,
                                const phys_body_t *bodies,
                                uint32_t body_count,
                                const phys_body_t *bodies_mut)
{
    if (!bodies_mut || !island || !bodies) {
        return false;
    }
    for (uint32_t b = 0; b < island->body_count; ++b) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        if (bodies[idx].tier == PHYS_TIER_ANIM && bodies[idx].inv_mass > 0.0f) {
            return true;
        }
    }
    return false;
}

/**
 * @brief Final position projection for coupled islands.
 *
 * After the iterative coupled solve, run one last FK propagation pass
 * to snap all joint anchors to exact coincidence.  This eliminates
 * any residual anchor drift from the iterative process.
 *
 * @param island      The island to project.
 * @param constraints Constraint array.
 * @param constraint_joint_indices Maps constraint → joint index.
 * @param joints      Joint array.
 * @param joint_count Number of joints.
 * @param bodies_mut  Mutable body array (positions updated in-place).
 * @param body_count  Number of bodies.
 */
static void coupled_position_projection_(
    const phys_island_t *island,
    phys_constraint_t *constraints,
    const uint32_t *constraint_joint_indices,
    phys_joint_t *joints,
    uint32_t joint_count,
    phys_body_t *bodies_mut,
    uint32_t body_count)
{
    if (!constraint_joint_indices || !joints || !bodies_mut) return;

    /* Multiple projection passes for convergence in long chains.
     * Each pass snaps child anchors to parents in topological order,
     * so a 20-bone chain converges in ~3 passes.  Use 5 to handle
     * branching chains (pelvis→leg + pelvis→spine). */
    for (int pass = 0; pass < 5; ++pass) {
        uint32_t last_ji = UINT32_MAX;
        for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
            uint32_t c_idx = island->constraint_indices[ci];
            phys_constraint_t *c = &constraints[c_idx];
            if (!c->is_joint) continue;

            uint32_t ji = constraint_joint_indices[c_idx];
            if (ji >= joint_count) continue;
            if (ji == last_ji) continue;
            last_ji = ji;

            phys_joint_t *j = &joints[ji];
            uint32_t ba = j->body_a;
            uint32_t bb = j->body_b;
            if (ba >= body_count || bb >= body_count) continue;
            if (bodies_mut[bb].inv_mass <= 0.0f) continue;

            /* Distance joints constrain separation, not coincidence. */
            if (j->type == PHYS_JOINT_DISTANCE) continue;

            /* Lock joints: snap orientation from parent. */
            if (j->type == PHYS_JOINT_LOCK) {
                bodies_mut[bb].orientation = quat_normalize_safe(
                    quat_mul(bodies_mut[ba].orientation,
                              j->rest_relative_orient),
                    1e-12f);
            }

            /* Angular limits are enforced by the solver's one-sided
             * angular rows (velocity-level corrections).  No orientation
             * clamping here — doing so without zeroing the corresponding
             * angular velocity creates energy injection (double-correction). */

            /* Snap child position so joint anchors coincide exactly. */
            phys_vec3_t wa = vec3_add(bodies_mut[ba].position,
                quat_rotate_vec3(bodies_mut[ba].orientation,
                                  j->local_anchor_a));
            phys_vec3_t child_anchor_world = quat_rotate_vec3(
                bodies_mut[bb].orientation, j->local_anchor_b);
            bodies_mut[bb].position = vec3_sub(wa, child_anchor_world);
        }
    }
}

/**
 * @brief Propagate position/orientation corrections through the joint
 *        hierarchy (forward kinematics).
 *
 * After the coupled solver updates body positions inline, parent bones
 * may have moved without their children following.  This pass snaps
 * each child body's position so its joint anchor coincides with the
 * parent's, and for lock joints also propagates orientation.
 *
 * Assumes joints are in topological (parent-before-child) order,
 * which is the natural skeleton ordering.
 */
static void propagate_coupled_anchors_(
    const phys_island_t *island,
    phys_constraint_t *constraints,
    const uint32_t *constraint_joint_indices,
    phys_joint_t *joints,
    uint32_t joint_count,
    phys_body_t *bodies_mut,
    uint32_t body_count)
{
    if (!constraint_joint_indices || !joints || !bodies_mut) return;

    uint32_t last_ji = UINT32_MAX;
    for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
        uint32_t c_idx = island->constraint_indices[ci];
        phys_constraint_t *c = &constraints[c_idx];
        if (!c->is_joint) continue;

        uint32_t ji = constraint_joint_indices[c_idx];
        if (ji >= joint_count) continue;
        /* Skip duplicate constraints for the same joint (e.g. hinge). */
        if (ji == last_ji) continue;
        last_ji = ji;

        phys_joint_t *j = &joints[ji];
        uint32_t ba = j->body_a;
        uint32_t bb = j->body_b;
        if (ba >= body_count || bb >= body_count) continue;
        if (bodies_mut[bb].inv_mass <= 0.0f) continue;

        /* Distance joints constrain separation, not anchor coincidence. */
        if (j->type == PHYS_JOINT_DISTANCE) continue;

        /* For lock joints: propagate orientation from parent. */
        if (j->type == PHYS_JOINT_LOCK) {
            bodies_mut[bb].orientation = quat_normalize_safe(
                quat_mul(bodies_mut[ba].orientation,
                          j->rest_relative_orient),
                1e-12f);
        }

        /* Snap child position so joint anchors coincide. */
        phys_vec3_t wa = vec3_add(bodies_mut[ba].position,
            quat_rotate_vec3(bodies_mut[ba].orientation,
                              j->local_anchor_a));
        phys_vec3_t child_anchor_world = quat_rotate_vec3(
            bodies_mut[bb].orientation, j->local_anchor_b);
        bodies_mut[bb].position = vec3_sub(wa, child_anchor_world);
    }
}

/**
 * @brief Recompute world-space inverse inertia for all dynamic island bodies.
 *
 * Called between coupled solver iterations after orientation changes.
 */
static void recompute_island_inertia_(const phys_island_t *island,
                                       const phys_body_t *bodies,
                                       phys_mat3_t *inv_inertia_world,
                                       uint32_t body_count)
{
    if (!inv_inertia_world) return;
    for (uint32_t b = 0; b < island->body_count; ++b) {
        uint32_t idx = island->body_indices[b];
        if (idx >= body_count) continue;
        const phys_body_t *body = &bodies[idx];
        if (body->inv_mass <= 0.0f) continue;
        inv_inertia_world[idx] = phys_mat3_inv_inertia_world(
            body->orientation, body->inv_inertia_diag);
    }
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

    /* Build rebuild args once for all coupled islands. */
    phys_constraint_rebuild_args_t rebuild_args = {
        .constraints             = args->constraints,
        .constraint_count        = UINT32_MAX, /* Constraint indices come from islands; no separate count in TGS args. */
        .constraint_joint_indices = args->constraint_joint_indices,
        .joints                  = args->joints,
        .joint_count             = args->joint_count,
        .bodies                  = args->bodies_mut ? args->bodies_mut : args->bodies,
        .body_count              = args->body_count,
        .manifolds               = args->manifolds,
        .manifold_count          = args->manifold_count,
        .inv_inertia_world       = args->inv_inertia_world,
        .dt                      = args->dt,
        .baumgarte               = args->baumgarte,
        .slop                    = args->slop,
    };

    /* Process each island independently. */
    for (uint32_t i = 0; i < islands->count; i++) {
        const phys_island_t *island = &islands->islands[i];
        if (island->sleeping || island->skip) continue;

        /* Skip XPBD islands — they are handled by Stage 11b. */
        if (island->constraint_count > 0 &&
            island_routes_xpbd_(island, args->constraints, args->bodies)) {
            continue;
        }

        /* Adaptive iteration count based on island body velocity. */
        uint32_t iters = compute_island_iterations(
            island, args->bodies, args->velocities, args->iterations);

        /* Check if this is a coupled (TIER_ANIM) island. */
        bool coupled = island_is_coupled_(island, args->bodies,
                                           args->body_count, args->bodies_mut);

        /* For large non-coupled islands with coloring enabled, use
         * graph-colored constraint ordering.  Coupled (TIER_ANIM) islands
         * skip coloring — they use the sparse CG solver which handles
         * the entire island simultaneously. */
        if (!coupled &&
            args->frame_arena &&
            args->island_color_threshold > 0 &&
            island->constraint_count >= args->island_color_threshold) {
            if (solve_island_colored(island, args, iters, slop, inv_dt)) {
                continue;
            }
        }

        /* Sequential solve: coupled islands use sparse CG for joints
         * (simultaneous solve of all joint constraints), then TGS for
         * contacts.  Non-coupled islands use pure GS as before. */
        if (coupled && args->frame_arena) {
            /* Allocate CG workspace from frame arena. */
            cg_system_t cg_sys;
            uint32_t max_rows = island->constraint_count *
                                PHYS_MAX_CONSTRAINT_ROWS;
            if (max_rows > CG_MAX_ROWS) max_rows = CG_MAX_ROWS;

            bool cg_ok = phys_cg_alloc(&cg_sys, args->frame_arena,
                                        max_rows);

            for (uint32_t iter = 0; iter < iters; iter++) {
                /* FK propagation + Jacobian rebuild. */
                propagate_coupled_anchors_(
                    island, args->constraints,
                    args->constraint_joint_indices,
                    args->joints, args->joint_count,
                    args->bodies_mut, args->body_count);
                recompute_island_inertia_(island, args->bodies_mut,
                                           args->inv_inertia_world_mut,
                                           args->body_count);
                phys_rebuild_island_all_constraints(island, &rebuild_args);

                if (cg_ok) {
                    /* Assemble sparse system A·λ = b for joint rows. */
                    const phys_mat3_t *inv_I_use =
                        args->inv_inertia_world_mut
                            ? args->inv_inertia_world_mut
                            : args->inv_inertia_world;

                    phys_cg_assemble(&cg_sys, island,
                                     args->constraints,
                                     args->bodies_mut,
                                     inv_I_use,
                                     args->velocities,
                                     args->body_count,
                                     args->dt);

                    if (cg_sys.n > 0 && !cg_sys.overflow) {
                        /* Lambda was zeroed by cg_assemble; CG solves
                         * for incremental Δλ directly. */
                        phys_cg_solve(&cg_sys, 40, 1e-6f);

                        /* Apply Δλ to velocities and positions. */
                        phys_cg_apply(&cg_sys, island,
                                      args->constraints,
                                      args->bodies_mut,
                                      inv_I_use,
                                      args->velocities,
                                      args->body_count,
                                      args->dt);
                    }
                }

                /* Contacts are now included in the CG system — no
                 * separate TGS pass needed. */

                /* Gyroscopic torque correction, once per body. */
                for (uint32_t b = 0; b < island->body_count; ++b) {
                    uint32_t idx = island->body_indices[b];
                    if (idx >= args->body_count) continue;
                    if (args->bodies_mut[idx].inv_mass <= 0.0f) continue;
                    apply_gyroscopic_correction(
                        &args->velocities[idx].angular,
                        args->bodies_mut[idx].inv_inertia_diag,
                        args->bodies_mut[idx].orientation,
                        args->dt);
                }
            }
        } else {
            /* Non-coupled: standard GS iteration. */
            for (uint32_t iter = 0; iter < iters; iter++) {
                for (uint32_t ci = 0; ci < island->constraint_count; ci++){
                    uint32_t c_idx = island->constraint_indices[ci];
                    solve_one_constraint(&args->constraints[c_idx],
                                         args->velocities, pseudo,
                                         args->bodies,
                                         args->inv_inertia_world,
                                         slop, inv_dt,
                                         args->tick_dt,
                                         args->tier_substep_counts,
                                         NULL);
                }
            }
        }

        /* Coupled islands: the solver wrote final positions directly
         * into bodies_mut.  Run position projection to snap anchors
         * to exact coincidence, write back solved velocities, and
         * mark bodies so the integrator skips position integration
         * (it would double-integrate otherwise). */
        if (coupled) {
            /* Final FK + anchor snap for near-zero anchor errors. */
            coupled_position_projection_(
                island, args->constraints,
                args->constraint_joint_indices,
                args->joints, args->joint_count,
                args->bodies_mut, args->body_count);

            for (uint32_t b = 0; b < island->body_count; ++b) {
                uint32_t idx = island->body_indices[b];
                if (idx >= args->body_count) continue;
                if (args->bodies_mut[idx].inv_mass <= 0.0f) continue;
                /* Write solved velocities back to body state. */
                args->bodies_mut[idx].linear_vel = args->velocities[idx].linear;
                args->bodies_mut[idx].angular_vel = args->velocities[idx].angular;
                /* Mark body so integrator copies velocities but
                 * does NOT re-integrate position (solver already did). */
                if (args->skip_body) {
                    args->skip_body[idx] = 1;
                }
            }
        }
    }

    /* Nonlinear joint position projection: after all TGS iterations,
     * recompute world anchors from predicted body state and apply
     * corrections that account for how rotation affects lever arms.
     * This fixes the angular popping that stale-Jacobian split impulse
     * can't handle for large joint violations at high speed. */
    if (pseudo && args->joints && args->joint_count > 0) {
        project_joints_nonlinear(args->joints, args->joint_count,
                                  args->bodies, pseudo,
                                  args->body_count, args->dt);
    }
}
