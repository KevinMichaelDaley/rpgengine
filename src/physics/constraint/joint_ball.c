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
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>

/** Large clamp value for bilateral lambda bounds. */
#define JOINT_LAMBDA_BIG 1e10f

/** Baumgarte factor for joint positional correction. */
#define JOINT_BAUMGARTE 0.2f

/**
 * @brief Rotate a vector by a quaternion: q * v * q^-1.
 */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v) {
    phys_vec3_t qv = {q.x, q.y, q.z};
    phys_vec3_t t = vec3_scale(vec3_cross(qv, v), 2.0f);
    return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}

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
                                 float dt) {
    memset(row, 0, sizeof(*row));

    row->J_va = vec3_scale(axis, -1.0f);
    row->J_wa = vec3_scale(vec3_cross(rA, axis), -1.0f);
    row->J_vb = axis;
    row->J_wb = vec3_cross(rB, axis);

    row->lambda_min = -JOINT_LAMBDA_BIG;
    row->lambda_max =  JOINT_LAMBDA_BIG;
    row->lambda = 0.0f;

    row->bias = (JOINT_BAUMGARTE / dt) * error;

    row->effective_mass = phys_compute_effective_mass(
        row,
        body_a->inv_mass, &body_a->inv_inertia_diag,
        body_b->inv_mass, &body_b->inv_inertia_diag);
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

    /* Positional error: how far anchor B is from anchor A. */
    phys_vec3_t error = vec3_sub(world_anchor_b, world_anchor_a);

    /* Lever arms from body centers to world anchors. */
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    /* Three rows: one per world axis. */
    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    for (int i = 0; i < 3; ++i) {
        float axis_error = vec3_dot(error, axes[i]);
        build_positional_row(&joint->rows[i], rA, rB, axes[i],
                             axis_error, body_a, body_b, dt);
    }

    joint->row_count = 3;
}
