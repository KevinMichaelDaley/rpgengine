/**
 * @file joint_motor.c
 * @brief Angular motor implementation for physics joints.
 *
 * Adds angular constraint rows to a joint that drive body B toward
 * a target orientation.  The orientation error is decomposed into
 * angle-axis form and split into X/Y/Z angular Jacobian rows.
 *
 * Non-static functions: 2 (phys_joint_motor_init, phys_joint_motor_apply)
 */

#include "ferrum/physics/joint_motor.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>
#include <math.h>

void phys_joint_motor_init(phys_joint_motor_t *motor) {
    if (!motor) return;
    memset(motor, 0, sizeof(*motor));
    motor->target_orientation = (phys_quat_t){0.f, 0.f, 0.f, 1.f};
}

/**
 * @brief Compute orientation error from body B's current to the target.
 *
 * Returns the error as an angle-axis vector (axis * angle), decomposed
 * into world-space X/Y/Z components.
 */
static phys_vec3_t compute_angular_error(phys_quat_t current,
                                          phys_quat_t target) {
    /* error_quat = target * inverse(current) */
    phys_quat_t inv_current = {-current.x, -current.y, -current.z, current.w};
    phys_quat_t err;
    err.w = target.w * inv_current.w - target.x * inv_current.x
          - target.y * inv_current.y - target.z * inv_current.z;
    err.x = target.w * inv_current.x + target.x * inv_current.w
          + target.y * inv_current.z - target.z * inv_current.y;
    err.y = target.w * inv_current.y - target.x * inv_current.z
          + target.y * inv_current.w + target.z * inv_current.x;
    err.z = target.w * inv_current.z + target.x * inv_current.y
          - target.y * inv_current.x + target.z * inv_current.w;

    /* Ensure shortest path (avoid 360° winding). */
    if (err.w < 0.f) {
        err.x = -err.x; err.y = -err.y;
        err.z = -err.z; err.w = -err.w;
    }

    /* Convert quaternion to angle-axis vector.
     * For small angles, sin(θ/2) ≈ θ/2, so axis*angle ≈ 2*(x,y,z). */
    float sin_half = sqrtf(err.x * err.x + err.y * err.y + err.z * err.z);
    if (sin_half < 1e-6f) {
        return (phys_vec3_t){0.f, 0.f, 0.f};
    }

    float half_angle = atan2f(sin_half, err.w);
    float angle = 2.f * half_angle;
    float inv_sin = 1.f / sin_half;

    return (phys_vec3_t){
        err.x * inv_sin * angle,
        err.y * inv_sin * angle,
        err.z * inv_sin * angle
    };
}

uint8_t phys_joint_motor_apply(const phys_joint_motor_t *motor,
                                struct phys_joint *joint,
                                const struct phys_body *body_a,
                                const struct phys_body *body_b,
                                float dt) {
    if (!motor || !joint || !body_a || !body_b || dt <= 0.f) return 0;
    if (motor->strength <= 0.f) return 0;
    if (joint->row_count + 3 > PHYS_JOINT_MAX_ROWS) return 0;

    /* Compute angular error: how far body B needs to rotate to reach target. */
    phys_vec3_t ang_error = compute_angular_error(body_b->orientation,
                                                   motor->target_orientation);

    /* Scale by motor strength. */
    ang_error = vec3_scale(ang_error, motor->strength);

    /* Precompute world-space inverse inertia for effective mass. */
    phys_mat3_t inv_i_world_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_world_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* Lambda bounds from max_torque. */
    float lam_max = (motor->max_torque > 0.f) ? motor->max_torque : 1e10f;

    /* Add 3 angular rows: one per world axis. */
    static const phys_vec3_t axes[3] = {
        {1.f, 0.f, 0.f},
        {0.f, 1.f, 0.f},
        {0.f, 0.f, 1.f},
    };

    float errors[3] = {ang_error.x, ang_error.y, ang_error.z};

    for (int i = 0; i < 3; i++) {
        phys_jacobian_row_t *row = &joint->rows[joint->row_count + i];
        memset(row, 0, sizeof(*row));

        /* Pure angular: no linear Jacobians. */
        row->J_va = (phys_vec3_t){0.f, 0.f, 0.f};
        row->J_vb = (phys_vec3_t){0.f, 0.f, 0.f};
        row->J_wa = vec3_scale(axes[i], -1.f);
        row->J_wb = axes[i];

        row->lambda_min = -lam_max;
        row->lambda_max =  lam_max;
        row->lambda = 0.f;
        row->bias = errors[i];
        row->damping = 0.3f;  /* Moderate damping for stability. */
        row->flags = PHYS_ROW_FLAG_ANGULAR;

        row->effective_mass = phys_compute_effective_mass(
            row,
            body_a->inv_mass, &inv_i_world_a,
            body_b->inv_mass, &inv_i_world_b);
    }

    joint->row_count += 3;
    return 3;
}
