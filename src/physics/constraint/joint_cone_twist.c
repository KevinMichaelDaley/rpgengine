/**
 * @file joint_cone_twist.c
 * @brief Build Jacobian rows for a cone-twist (limited ball) joint.
 *
 * Produces 3 positional rows locking anchor points (like ball),
 * plus up to 3 one-sided angular rows enforcing per-axis rotation
 * limits relative to the joint's rest orientation.  This is the
 * standard ragdoll joint: free position lock with angular freedom
 * bounded by per-axis min/max limits.
 *
 * Non-static functions: 1 (phys_joint_build_cone_twist)
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <string.h>

#define JOINT_LAMBDA_BIG 1e10f

/**
 * @brief Build a bilateral positional row along a world axis.
 */
static void build_positional_row(phys_jacobian_row_t *row,
                                 phys_vec3_t rA, phys_vec3_t rB,
                                 phys_vec3_t axis, float error,
                                 const struct phys_body *body_a,
                                 const struct phys_body *body_b,
                                 const phys_mat3_t *inv_i_world_a,
                                 const phys_mat3_t *inv_i_world_b,
                                 float row_damping) {
    memset(row, 0, sizeof(*row));
    row->J_va = vec3_scale(axis, -1.0f);
    row->J_wa = vec3_scale(vec3_cross(rA, axis), -1.0f);
    row->J_vb = axis;
    row->J_wb = vec3_cross(rB, axis);
    row->lambda_min = -JOINT_LAMBDA_BIG;
    row->lambda_max =  JOINT_LAMBDA_BIG;
    row->lambda = 0.0f;
    row->bias = error;
    row->constraint_error = error;
    row->damping = row_damping;
    row->effective_mass = phys_compute_effective_mass(
        row, body_a->inv_mass, inv_i_world_a,
        body_b->inv_mass, inv_i_world_b);
}

/**
 * @brief Decompose error quaternion into swing-twist representation.
 *
 * The twist axis is the joint's local X axis (index 0).  The error
 * quaternion q is factored as q = q_swing * q_twist, where:
 *   q_twist = normalize(q.w, q.x, 0, 0)  (rotation about X)
 *   q_swing = q * conj(q_twist)           (rotation in YZ plane)
 *
 * Returns:
 *   twist_angle  — signed rotation about X (radians)
 *   swing_y      — swing component about Y (radians)
 *   swing_z      — swing component about Z (radians)
 *
 * Unlike rotation-vector decomposition, swing-twist components are
 * geometrically independent: twist doesn't affect swing and vice versa.
 * This avoids the axis coupling that causes energy injection at large
 * angles with rotation-vector decomposition.
 */
static void quat_to_swing_twist(phys_quat_t q,
                                 float *twist_angle,
                                 float *swing_y,
                                 float *swing_z)
{
    /* Extract twist quaternion: projection of q onto twist axis (X).
     * q_twist = normalize(w, x, 0, 0). */
    float tw = q.w, tx = q.x;
    float twist_len = sqrtf(tw * tw + tx * tx);
    if (twist_len > 1e-8f) {
        tw /= twist_len;
        tx /= twist_len;
    } else {
        /* Degenerate: 180° swing, no twist info recoverable. */
        tw = 1.0f;
        tx = 0.0f;
    }

    /* Twist angle from the twist quaternion. */
    *twist_angle = 2.0f * atan2f(tx, tw);

    /* Swing quaternion: q_swing = q * conj(q_twist).
     * conj(q_twist) = (tw, -tx, 0, 0). */
    phys_quat_t q_twist_conj = {-tx, 0.0f, 0.0f, tw};
    phys_quat_t q_swing = quat_mul(q, q_twist_conj);

    /* Swing Y and Z from the swing quaternion's axis-angle.
     * q_swing ≈ (cos(θ/2), 0, sin(θ/2)·ŷ, sin(θ/2)·ẑ) for small swing.
     * For arbitrary swing, extract the full rotation vector. */
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

void phys_joint_build_cone_twist(phys_joint_t *joint,
                                 const struct phys_body *body_a,
                                 const struct phys_body *body_b,
                                 float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* ---- Positional rows (same as ball joint) ---- */

    /* World-space anchors. */
    phys_vec3_t world_anchor_a = vec3_add(
        body_a->position,
        quat_rotate_vec3(body_a->orientation, joint->local_anchor_a));
    phys_vec3_t world_anchor_b = vec3_add(
        body_b->position,
        quat_rotate_vec3(body_b->orientation, joint->local_anchor_b));

    phys_vec3_t pred_pos_a = vec3_add(body_a->position,
                                       vec3_scale(body_a->linear_vel, dt));
    phys_vec3_t pred_pos_b = vec3_add(body_b->position,
                                       vec3_scale(body_b->linear_vel, dt));
    phys_vec3_t pred_anchor_a = vec3_add(
        pred_pos_a,
        quat_rotate_vec3(body_a->orientation, joint->local_anchor_a));
    phys_vec3_t pred_anchor_b = vec3_add(
        pred_pos_b,
        quat_rotate_vec3(body_b->orientation, joint->local_anchor_b));

    phys_vec3_t curr_error = vec3_sub(world_anchor_b, world_anchor_a);
    phys_vec3_t pred_error = vec3_sub(pred_anchor_b, pred_anchor_a);
    float spd2_a = vec3_dot(body_a->linear_vel, body_a->linear_vel);
    float spd2_b = vec3_dot(body_b->linear_vel, body_b->linear_vel);
    float max_spd2 = spd2_a > spd2_b ? spd2_a : spd2_b;
    const float pred_lo2 = 5.0f * 5.0f;
    const float pred_hi2 = 80.0f * 80.0f;
    float pred_blend = 0.0f;
    if (max_spd2 > pred_lo2) {
        pred_blend = (max_spd2 - pred_lo2) / (pred_hi2 - pred_lo2);
        if (pred_blend > 0.5f) { pred_blend = 0.5f; }
    }
    phys_vec3_t error = vec3_add(
        vec3_scale(curr_error, 1.0f - pred_blend),
        vec3_scale(pred_error, pred_blend));
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    phys_mat3_t inv_i_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    /* Rows 0-2: positional lock along X, Y, Z.
     * When LINEAR_DRIVE is set, these rows are flagged as drive rows
     * so the solver uses drive_compliance (soft spring) instead of
     * the joint's hard compliance. */
    uint8_t linear_drive = (joint->flags & PHYS_JOINT_FLAG_LINEAR_DRIVE)
                           ? PHYS_ROW_FLAG_DRIVE : 0;
    for (int i = 0; i < 3; ++i) {
        float axis_error = vec3_dot(error, axes[i]);
        build_positional_row(&joint->rows[i], rA, rB, axes[i],
                             axis_error, body_a, body_b,
                             &inv_i_a, &inv_i_b, joint->damping);
        joint->rows[i].lambda = joint->cached_lambda[i];
        joint->rows[i].flags |= linear_drive;
    }

    /* ---- Angular limit / drive rows ---- */

    /* Compute relative rotation error relative to rest pose.
     * q_current = q_b * conj(q_a) is the current relative orientation.
     * q_error = conj(q_rest) * q_current measures deviation from rest. */
    phys_quat_t q_current = quat_normalize_safe(
        quat_mul(body_b->orientation, quat_conjugate(body_a->orientation)),
        1e-12f);
    phys_quat_t q_error = quat_normalize_safe(
        quat_mul(quat_conjugate(joint->rest_relative_orient), q_current),
        1e-12f);

    /* Ensure shortest path. */
    if (q_error.w < 0.0f) {
        q_error.x = -q_error.x; q_error.y = -q_error.y;
        q_error.z = -q_error.z; q_error.w = -q_error.w;
    }

    /* The joint frame in world space: parent orientation × rest pose.
     * Angular errors are measured in this frame, so the Jacobian axes
     * must also be expressed in this frame to get correct torque
     * directions.  Without this, limits are enforced along world axes
     * and appear to "detach" from the parent when it rotates. */
    phys_quat_t joint_frame = quat_normalize_safe(
        quat_mul(body_a->orientation, joint->rest_relative_orient),
        1e-12f);

    uint8_t angular_drive = (joint->flags & PHYS_JOINT_FLAG_ANGULAR_DRIVE)
                            ? 1 : 0;

    /* Swing-twist decomposition of the error quaternion.
     *
     * The twist axis is the joint's local X (bone longitudinal axis).
     * Swing is the rotation of that axis in the YZ plane.  Unlike
     * rotation-vector decomposition, swing and twist are geometrically
     * independent — changing twist doesn't affect swing and vice versa.
     * This eliminates cross-axis coupling that causes energy injection
     * at large angles, and reduces 3 overconstrained rows to 2-3
     * properly independent DOFs. */
    float twist_angle, swing_y, swing_z;
    quat_to_swing_twist(q_error, &twist_angle, &swing_y, &swing_z);

    /* Map swing-twist components to the limit axes:
     *   axis 0 (X) → twist
     *   axis 1 (Y) → swing_y
     *   axis 2 (Z) → swing_z */
    float st_angles[3] = {twist_angle, swing_y, swing_z};

    uint8_t rc = 3;  /* First 3 rows are positional. */
    for (int i = 0; i < 3; ++i) {
        if (!(joint->limit_axes & (1u << i))) continue;

        float angle = st_angles[i];
        float lo = joint->limit_min[i];
        float hi = joint->limit_max[i];

        float ang_error = 0.0f;
        float lmin = 0.0f, lmax = 0.0f;
        uint8_t is_drive = 0;

        if (angle < lo) {
            ang_error = angle - lo;
            lmin = 0.0f;
            lmax = JOINT_LAMBDA_BIG;
        } else if (angle > hi) {
            ang_error = angle - hi;
            lmin = -JOINT_LAMBDA_BIG;
            lmax = 0.0f;
        } else {
            /* Within limits — bilateral speculative row.  Bias is zero
             * but the row is active so the CG solver can resist external
             * forces in either direction. */
            ang_error = 0.0f;
            if (angular_drive) {
                is_drive = 1;
            }
            lmin = -JOINT_LAMBDA_BIG;
            lmax =  JOINT_LAMBDA_BIG;
        }

        /* Transform the limit axis from joint-local to world space. */
        phys_vec3_t world_axis = quat_rotate_vec3(joint_frame, axes[i]);

        phys_jacobian_row_t *row = &joint->rows[rc];
        memset(row, 0, sizeof(*row));
        /* Lever-arm cross products on ALL angular rows because the
         * joint anchor is off the body's COM. */
        row->J_va = vec3_scale(vec3_cross(rA, world_axis), -1.0f);
        row->J_vb = vec3_cross(rB, world_axis);
        row->J_wa = vec3_scale(world_axis, -1.0f);
        row->J_wb = world_axis;
        row->lambda_min = lmin;
        row->lambda_max = lmax;
        row->lambda = 0.0f;
        row->bias = ang_error;
        row->constraint_error = ang_error;
        row->damping = joint->damping;
        row->flags = PHYS_ROW_FLAG_ANGULAR
                   | (is_drive ? PHYS_ROW_FLAG_DRIVE : 0);

        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_a,
            body_b->inv_mass, &inv_i_b);
        rc++;
    }

    joint->row_count = rc;
}
