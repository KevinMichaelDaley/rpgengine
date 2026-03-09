/**
 * @file joint_angular_projection.c
 * @brief Post-solve angular position projection for joint limits.
 *
 * After the TGS velocity solve, joint angles can exceed their limits
 * due to high-impulse contact events.  The velocity-level solver only
 * prevents further angular velocity in the violating direction — it
 * doesn't snap the angle back.  This module does the snapping by
 * computing the angular overshoot and injecting corrective angular
 * pseudo-velocity, which the integrator applies to orientation.
 *
 * Non-static functions: 1 (phys_project_joint_angular_limits)
 */

#include "ferrum/physics/joint_angular_projection.h"

#include <math.h>
#include <stddef.h>

#include <stdio.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/tgs_solve.h"   /* phys_velocity_t */
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/** Number of projection passes (converge long chains).
 * More passes needed than linear projection because angular violations
 * from contact impacts can be several radians. */
#define ANG_PROJ_PASSES 8

/** Fraction of angular error corrected per pass. */
#define ANG_PROJ_FRACTION 0.9f

/** Minimum angular error (radians) worth correcting. */
#define ANG_PROJ_MIN_ERROR 0.001f

/**
 * @brief Integrate a quaternion by an angular velocity over dt.
 *
 * Uses the standard quaternion derivative: dq = 0.5 * omega_q * q.
 */
static phys_quat_t ang_proj_quat_integrate(phys_quat_t q, phys_vec3_t w,
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
 * @brief Decompose error quaternion into swing-twist angles.
 *
 * Twist axis = joint local X.  Same decomposition as joint_cone_twist.c.
 *
 * @param q            Error quaternion (normalized, shortest path).
 * @param twist_angle  Output: signed twist about X (radians).
 * @param swing_y      Output: swing about Y (radians).
 * @param swing_z      Output: swing about Z (radians).
 */
static void swing_twist_decompose(phys_quat_t q,
                                   float *twist_angle,
                                   float *swing_y,
                                   float *swing_z)
{
    /* Extract twist quaternion: projection onto X axis. */
    float tw = q.w, tx = q.x;
    float twist_len = sqrtf(tw * tw + tx * tx);
    if (twist_len > 1e-8f) {
        tw /= twist_len;
        tx /= twist_len;
    } else {
        tw = 1.0f;
        tx = 0.0f;
    }

    *twist_angle = 2.0f * atan2f(tx, tw);

    /* Swing quaternion: q_swing = q * conj(q_twist). */
    phys_quat_t q_twist_conj = { -tx, 0.0f, 0.0f, tw };
    phys_quat_t q_swing = quat_mul(q, q_twist_conj);

    float sy = q_swing.y, sz = q_swing.z, sw = q_swing.w;
    float sin_half = sqrtf(sy * sy + sz * sz);
    if (sin_half > 1e-8f) {
        float half_angle = atan2f(sin_half, fabsf(sw));
        float scale = 2.0f * half_angle / sin_half;
        if (sw < 0.0f) scale = -scale;
        *swing_y = sy * scale;
        *swing_z = sz * scale;
    } else {
        *swing_y = 2.0f * sy;
        *swing_z = 2.0f * sz;
    }
}

/**
 * @brief Compute angular limit violations and return the correction
 *        rotation vector in world space.
 *
 * For each limited axis, if the angle exceeds [lo, hi], the correction
 * is the amount needed to bring it back to the nearest boundary.
 *
 * @param j         Joint with angular limits.
 * @param ori_a     Predicted orientation of body A.
 * @param ori_b     Predicted orientation of body B.
 * @param correction_out  Output: world-space angular correction (radians).
 * @return true if any axis was violated and correction is nonzero.
 */
static bool compute_angular_correction(const phys_joint_t *j,
                                        phys_quat_t ori_a,
                                        phys_quat_t ori_b,
                                        phys_vec3_t *correction_out)
{
    /* Relative orientation error from rest pose. */
    phys_quat_t q_current = quat_normalize_safe(
        quat_mul(ori_b, quat_conjugate(ori_a)), 1e-12f);
    phys_quat_t q_error = quat_normalize_safe(
        quat_mul(quat_conjugate(j->rest_relative_orient), q_current),
        1e-12f);

    /* Shortest path. */
    if (q_error.w < 0.0f) {
        q_error.x = -q_error.x; q_error.y = -q_error.y;
        q_error.z = -q_error.z; q_error.w = -q_error.w;
    }

    /* Joint frame in world space. */
    phys_quat_t joint_frame = quat_normalize_safe(
        quat_mul(ori_a, j->rest_relative_orient), 1e-12f);

    /* Swing-twist decomposition. */
    float twist_angle, swing_y, swing_z;
    swing_twist_decompose(q_error, &twist_angle, &swing_y, &swing_z);

    float st_angles[3] = { twist_angle, swing_y, swing_z };

    static const phys_vec3_t local_axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    /* Accumulate correction in world space. */
    phys_vec3_t correction = {0.0f, 0.0f, 0.0f};
    bool any_violation = false;

    for (int i = 0; i < 3; ++i) {
        if (!(j->limit_axes & (1u << i))) continue;

        float angle = st_angles[i];
        float lo = j->limit_min[i];
        float hi = j->limit_max[i];
        float excess = 0.0f;

        if (angle < lo) {
            excess = lo - angle;  /* positive: rotate toward lo */
        } else if (angle > hi) {
            excess = hi - angle;  /* negative: rotate toward hi */
        }

        if (fabsf(excess) < ANG_PROJ_MIN_ERROR) continue;

        any_violation = true;

        /* Transform axis from joint-local to world space. */
        phys_vec3_t world_axis = quat_rotate_vec3(joint_frame,
                                                    local_axes[i]);

        /* Correction in world space along this axis. */
        correction = vec3_add(correction,
                              vec3_scale(world_axis, excess));
    }

    *correction_out = correction;
    return any_violation;
}

void phys_project_joint_angular_limits(
    const phys_angular_projection_args_t *args)
{
    if (!args || !args->joints || !args->bodies ||
        !args->velocities || !args->pseudo_velocities ||
        args->joint_count == 0 || args->dt <= 0.0f) {
        return;
    }

    const float dt = args->dt;
    const float inv_dt = 1.0f / dt;

    for (uint32_t pass = 0; pass < ANG_PROJ_PASSES; pass++) {
        for (uint32_t ji = 0; ji < args->joint_count; ji++) {
            const phys_joint_t *j = &args->joints[ji];

            /* Only cone-twist and hinge joints have angular limits
             * that need position-level projection. */
            if (j->type != PHYS_JOINT_CONE_TWIST &&
                j->type != PHYS_JOINT_HINGE &&
                j->type != PHYS_JOINT_LIMIT_ROTATION) {
                continue;
            }

            if (j->limit_axes == 0) continue;
            if (j->body_a >= args->body_count ||
                j->body_b >= args->body_count) {
                continue;
            }

            const phys_body_t *ba = &args->bodies[j->body_a];
            const phys_body_t *bb = &args->bodies[j->body_b];

            /* Predict orientations after integrating solved angular
             * velocities + accumulated pseudo-velocities.  Use the
             * solved velocities (from TGS output), not the body's
             * stored angular_vel which is from the previous substep. */
            phys_vec3_t total_ang_a = vec3_add(
                args->velocities[j->body_a].angular,
                args->pseudo_velocities[j->body_a].angular);
            phys_vec3_t total_ang_b = vec3_add(
                args->velocities[j->body_b].angular,
                args->pseudo_velocities[j->body_b].angular);

            phys_quat_t ori_a = ang_proj_quat_integrate(
                ba->orientation, total_ang_a, dt);
            phys_quat_t ori_b = ang_proj_quat_integrate(
                bb->orientation, total_ang_b, dt);

            /* Compute angular correction needed. */
            phys_vec3_t correction;
            if (!compute_angular_correction(j, ori_a, ori_b,
                                             &correction)) {
                continue;
            }

            if (pass == 0) {
                float mag = sqrtf(vec3_dot(correction, correction));
                fprintf(stderr, "[ANG-PROJ] j%u pass=%u corr_mag=%.4f\n",
                        ji, pass, mag);
            }

            /* Scale by fraction for stability. */
            correction = vec3_scale(correction, ANG_PROJ_FRACTION);

            /* Distribute correction between bodies weighted by
             * inverse mass (as a proxy for inverse inertia).
             * Static bodies (inv_mass == 0) get no correction. */
            float im_a = ba->inv_mass;
            float im_b = bb->inv_mass;
            float im_total = im_a + im_b;
            if (im_total < 1e-12f) continue;

            float frac_a = im_a / im_total;
            float frac_b = im_b / im_total;

            /* Convert angular position correction to pseudo-velocity.
             * Body A rotates in the positive correction direction,
             * body B rotates in the negative direction (they rotate
             * toward each other to close the violation). */
            if (im_a > 0.0f) {
                phys_vec3_t ang_corr = vec3_scale(correction,
                                                   frac_a * inv_dt);
                args->pseudo_velocities[j->body_a].angular =
                    vec3_add(args->pseudo_velocities[j->body_a].angular,
                             ang_corr);
            }
            if (im_b > 0.0f) {
                phys_vec3_t ang_corr = vec3_scale(correction,
                                                   -frac_b * inv_dt);
                args->pseudo_velocities[j->body_b].angular =
                    vec3_add(args->pseudo_velocities[j->body_b].angular,
                             ang_corr);
            }
        }
    }
}
