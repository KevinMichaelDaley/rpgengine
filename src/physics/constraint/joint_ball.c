/**
 * @file joint_ball.c
 * @brief Ball (spherical) joint constraint row builder.
 *
 * Produces 3 bilateral Jacobian rows locking anchor points along
 * the X, Y, and Z world axes.  Bias corrects positional error
 * between the two world-space anchor points.
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>

/** Large clamp value for bilateral lambda bounds. */
#define JOINT_LAMBDA_BIG 1e10f



/**
 * @brief Build one positional row along a world axis.
 *
 * @param row     Output Jacobian row.
 * @param rA      Lever arm from body A center to world anchor A.
 * @param rB      Lever arm from body B center to world anchor B.
 * @param axis    World-space constraint direction (unit vector).
 * @param error   Positional error along this axis.
 * @param body_a  Body A (for effective mass).
 * @param body_b  Body B (for effective mass).
 * @param dt      Timestep.
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
    row->lambda = 0.0f;  /* Overwritten by warmstart in build functions. */

    /* Position error stored in bias for split-impulse correction.
     * The velocity-level solve sees bias=0 (set by the solver);
     * position correction uses this raw error value. */
    row->bias = error;
    row->damping = row_damping;

    row->effective_mass = phys_compute_effective_mass(
        row,
        body_a->inv_mass, inv_i_world_a,
        body_b->inv_mass, inv_i_world_b);
}

void phys_joint_build_ball(phys_joint_t *joint,
                           const struct phys_body *body_a,
                           const struct phys_body *body_b,
                           float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* Compute world-space anchor positions. */
    phys_vec3_t world_anchor_a = vec3_add(
        body_a->position,
        quat_rotate_vec3(body_a->orientation, joint->local_anchor_a));
    phys_vec3_t world_anchor_b = vec3_add(
        body_b->position,
        quat_rotate_vec3(body_b->orientation, joint->local_anchor_b));

    /* Predicted anchor positions at t+dt for bodies with significant
     * velocity.  Using the predicted error steers the constraint toward
     * where the anchors will be, not where they were — critical for
     * fast-moving articulated bodies. */
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

    /* Blend current and predicted error.  At low speeds, use current
     * error (accurate).  At high speeds, shift toward predicted. */
    phys_vec3_t curr_error = vec3_sub(world_anchor_b, world_anchor_a);
    phys_vec3_t pred_error = vec3_sub(pred_anchor_b, pred_anchor_a);
    float spd2_a = vec3_dot(body_a->linear_vel, body_a->linear_vel);
    float spd2_b = vec3_dot(body_b->linear_vel, body_b->linear_vel);
    float max_spd2 = spd2_a > spd2_b ? spd2_a : spd2_b;

    /* Blend factor: 0 below 5 m/s, ramps to 0.5 at 80 m/s. */
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

    /* Lever arms from body centers to world anchors. */
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    /* Precompute world-space inverse inertia for effective mass. */
    phys_mat3_t inv_i_world_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_world_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* Three rows: one per world axis. */
    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    /* When LINEAR_DRIVE is set, flag positional rows so the solver
     * uses drive_compliance (soft spring) instead of hard compliance. */
    uint8_t linear_drive = (joint->flags & PHYS_JOINT_FLAG_LINEAR_DRIVE)
                           ? PHYS_ROW_FLAG_DRIVE : 0;
    for (int i = 0; i < 3; ++i) {
        float axis_error = vec3_dot(error, axes[i]);
        build_positional_row(&joint->rows[i], rA, rB, axes[i],
                             axis_error, body_a, body_b,
                             &inv_i_world_a, &inv_i_world_b,
                             joint->damping);
        /* Warmstart: seed lambda from previous substep's cached value. */
        joint->rows[i].lambda = joint->cached_lambda[i];
        joint->rows[i].flags |= linear_drive;
    }

    joint->row_count = 3;
}
