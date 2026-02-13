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

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_color.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/** Minimum penetration excess to correct (avoids micro-jitter). */
#define SPLIT_MIN_PHI 1e-6f

/** Speed (m/s) above which we start adding solver iterations. */
#define ADAPTIVE_SPEED_LOW  5.0f
/** Speed (m/s) at which we reach maximum solver iterations. */
#define ADAPTIVE_SPEED_HIGH 200.0f
/** Maximum multiplier on base iteration count for fast islands. */
#define ADAPTIVE_ITER_MULT  5

/** Successive over-relaxation factor.  Values > 1.0 accelerate
 *  convergence; typical range 1.1–1.5.  Too high causes oscillation. */
#define SOR_OMEGA 1.3f

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
        return base_iters;
    }
    if (max_speed_sq >= hi2) {
        return base_iters * ADAPTIVE_ITER_MULT;
    }

    /* Sqrt ramp: aggressive at moderate speeds, plateaus at extremes. */
    float t = (max_speed_sq - lo2) / (hi2 - lo2);
    t = sqrtf(t);
    uint32_t extra = (uint32_t)(t * (float)(base_iters * (ADAPTIVE_ITER_MULT - 1)));
    return base_iters + extra;
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

    /* Impulse delta from constraint violation.
     * Linear viscous damping: opposes relative velocity along the
     * constraint axis proportional to speed.  damping=0 is off,
     * damping=1 doubles the effective velocity correction. */
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
                                      const phys_vec3_t *inv_i_a,
                                      float inv_mass_b,
                                      const phys_vec3_t *inv_i_b)
{
    /* row->bias holds the raw position error (meters, signed). */
    float error = row->bias;
    if (fabsf(error) < 1e-7f) { return; }

    /* Target pseudo-velocity to correct the error in one substep.
     * Negative sign: positive error means anchors are separated, so
     * correction drives the relative anchor velocity negative. */
    float pos_bias = -error * inv_dt;

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

    pva->angular.x += inv_i_a->x * row->J_wa.x * delta_lambda;
    pva->angular.y += inv_i_a->y * row->J_wa.y * delta_lambda;
    pva->angular.z += inv_i_a->z * row->J_wa.z * delta_lambda;

    pvb->angular.x += inv_i_b->x * row->J_wb.x * delta_lambda;
    pvb->angular.y += inv_i_b->y * row->J_wb.y * delta_lambda;
    pvb->angular.z += inv_i_b->z * row->J_wb.z * delta_lambda;
}

/* ── Nonlinear joint position projection ──────────────────────────── */

/** Minimum anchor error (meters) to trigger nonlinear projection. */
#define NL_PROJ_MIN_ERROR 0.01f

/** Number of nonlinear projection passes after TGS iterations. */
#define NL_PROJ_PASSES 4

/** Fraction of error corrected per nonlinear projection pass. */
#define NL_PROJ_FRACTION 0.8f

/**
 * @brief Rotate a vector by a quaternion: q * v * q^-1.
 */
static phys_vec3_t tgs_quat_rotate(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t qv = {q.x, q.y, q.z};
    phys_vec3_t t = vec3_scale(vec3_cross(qv, v), 2.0f);
    return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}

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
            phys_vec3_t rA = tgs_quat_rotate(ori_a, j->local_anchor_a);
            phys_vec3_t rB = tgs_quat_rotate(ori_b, j->local_anchor_b);
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

/* ── Solve one constraint (all rows) ──────────────────────────────── */

/**
 * @brief Solve all rows of a single constraint: normal + friction + split.
 */
static void solve_one_constraint(phys_constraint_t *c,
                                  phys_velocity_t *velocities,
                                  phys_velocity_t *pseudo,
                                  const struct phys_body *bodies,
                                  float slop,
                                  float inv_dt)
{
    phys_velocity_t *va = &velocities[c->body_a];
    phys_velocity_t *vb = &velocities[c->body_b];

    float inv_mass_a = bodies[c->body_a].inv_mass;
    float inv_mass_b = bodies[c->body_b].inv_mass;
    const phys_vec3_t *inv_i_a = &bodies[c->body_a].inv_inertia_diag;
    const phys_vec3_t *inv_i_b = &bodies[c->body_b].inv_inertia_diag;

    if (c->is_joint) {
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
         * to Baumgarte leak: -error * inv_dt * baumgarte_fraction. */
        float saved_bias[PHYS_MAX_CONSTRAINT_ROWS];
        for (uint8_t r = 0; r < c->row_count; r++) {
            saved_bias[r] = c->rows[r].bias;
            c->rows[r].bias = -saved_bias[r] * inv_dt * baumgarte;
        }

        /* Velocity-level solve: all rows with bilateral bounds. */
        for (uint8_t r = 0; r < c->row_count; r++) {
            solve_row(&c->rows[r], va, vb,
                      inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
        }

        /* Restore bias and apply split-impulse position correction. */
        for (uint8_t r = 0; r < c->row_count; r++) {
            c->rows[r].bias = saved_bias[r];
        }
        if (pseudo) {
            for (uint8_t r = 0; r < c->row_count; r++) {
                solve_joint_position_row(
                    &c->rows[r],
                    &pseudo[c->body_a], &pseudo[c->body_b],
                    inv_dt,
                    inv_mass_a, inv_i_a, inv_mass_b, inv_i_b);
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

    /* Solve per iteration, per color batch. */
    phys_velocity_t *pseudo = args->pseudo_velocities;

    for (uint32_t iter = 0; iter < iters; ++iter) {
        for (uint32_t color = 0; color < coloring.num_colors; ++color) {
            /* Solve all constraints with this color. */
            for (uint32_t ci = 0; ci < n; ++ci) {
                if (coloring.colors[ci] != color) { continue; }
                uint32_t c_idx = island->constraint_indices[ci];
                solve_one_constraint(&args->constraints[c_idx],
                                     args->velocities, pseudo,
                                     args->bodies, slop, inv_dt);
            }
        }
    }

    return true;
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

        /* Adaptive iteration count based on island body velocity. */
        uint32_t iters = compute_island_iterations(
            island, args->bodies, args->velocities, args->iterations);

        /* For large islands with coloring enabled, use graph-colored
         * constraint ordering.  Falls back to sequential on failure. */
        if (args->frame_arena &&
            args->island_color_threshold > 0 &&
            island->constraint_count >= args->island_color_threshold) {
            if (solve_island_colored(island, args, iters, slop, inv_dt)) {
                continue;
            }
        }

        /* Sequential solve (default path). */
        for (uint32_t iter = 0; iter < iters; iter++) {
            for (uint32_t ci = 0; ci < island->constraint_count; ci++) {
                uint32_t c_idx = island->constraint_indices[ci];
                solve_one_constraint(&args->constraints[c_idx],
                                     args->velocities, pseudo,
                                     args->bodies, slop, inv_dt);
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
