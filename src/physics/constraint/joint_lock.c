/**
 * @file joint_lock.c
 * @brief Build Jacobian rows for a lock (rigid attachment) joint.
 *
 * Produces 6 bilateral rows: 3 positional (like ball) + 3 angular
 * that lock all relative rotation between body_a and body_b.
 * Used for Copy Transforms / Child Of animation constraints.
 *
 * Non-static functions: 1 (phys_joint_build_lock)
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
    row->damping = row_damping;
    row->effective_mass = phys_compute_effective_mass(
        row, body_a->inv_mass, inv_i_world_a,
        body_b->inv_mass, inv_i_world_b);
}

/**
 * @brief Build a bilateral angular row along a world axis.
 *
 * Constrains relative angular velocity along the given axis.
 * Bias is set to the angular error on that axis for position-level
 * drift correction.
 */
static void build_angular_row(phys_jacobian_row_t *row,
                               phys_vec3_t axis, float angular_error,
                               const struct phys_body *body_a,
                               const struct phys_body *body_b,
                               const phys_mat3_t *inv_i_world_a,
                               const phys_mat3_t *inv_i_world_b,
                               float row_damping) {
    memset(row, 0, sizeof(*row));
    row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    row->J_wa = vec3_scale(axis, -1.0f);
    row->J_wb = axis;
    row->lambda_min = -JOINT_LAMBDA_BIG;
    row->lambda_max =  JOINT_LAMBDA_BIG;
    row->lambda = 0.0f;
    row->bias = angular_error;
    row->damping = row_damping;
    row->flags = PHYS_ROW_FLAG_ANGULAR;
    row->effective_mass = phys_compute_effective_mass(
        row, body_a->inv_mass, inv_i_world_a,
        body_b->inv_mass, inv_i_world_b);
}

void phys_joint_build_lock(phys_joint_t *joint,
                           const struct phys_body *body_a,
                           const struct phys_body *body_b,
                           float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* World-space anchors. */
    phys_vec3_t world_anchor_a = vec3_add(
        body_a->position,
        quat_rotate_vec3(body_a->orientation, joint->local_anchor_a));
    phys_vec3_t world_anchor_b = vec3_add(
        body_b->position,
        quat_rotate_vec3(body_b->orientation, joint->local_anchor_b));

    phys_vec3_t pos_error = vec3_sub(world_anchor_b, world_anchor_a);
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    phys_mat3_t inv_i_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* Rows 0-2: positional lock along X, Y, Z. */
    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };
    for (int i = 0; i < 3; ++i) {
        float err = vec3_dot(pos_error, axes[i]);
        build_positional_row(&joint->rows[i], rA, rB, axes[i], err,
                             body_a, body_b, &inv_i_a, &inv_i_b,
                             joint->damping);
        joint->rows[i].lambda = joint->cached_lambda[i];
    }

    /* Rows 3-5: angular lock along X, Y, Z.
     * Compute relative rotation error against rest pose:
     * q_current = q_b * conjugate(q_a) is the live relative orientation.
     * q_error = conjugate(q_rest) * q_current measures deviation from
     * the rest orientation.  The vector part of q_error (scaled by 2)
     * approximates the rotation error for small angles. */
    phys_quat_t q_current = quat_normalize_safe(
        quat_mul(body_b->orientation, quat_conjugate(body_a->orientation)),
        1e-12f);
    phys_quat_t q_error = quat_normalize_safe(
        quat_mul(quat_conjugate(joint->rest_relative_orient), q_current),
        1e-12f);
    /* Ensure shortest path (avoid 360-degree flip). */
    if (q_error.w < 0.0f) {
        q_error.x = -q_error.x; q_error.y = -q_error.y;
        q_error.z = -q_error.z; q_error.w = -q_error.w;
    }
    /* Angular error vector ~ 2 * vec(q_error) for small angles. */
    phys_vec3_t ang_err = {2.0f * q_error.x, 2.0f * q_error.y, 2.0f * q_error.z};

    for (int i = 0; i < 3; ++i) {
        float err = vec3_dot(ang_err, axes[i]);
        build_angular_row(&joint->rows[3 + i], axes[i], err,
                          body_a, body_b, &inv_i_a, &inv_i_b,
                          joint->damping);
        joint->rows[3 + i].lambda = joint->cached_lambda[3 + i];
    }

    joint->row_count = 6;
}
