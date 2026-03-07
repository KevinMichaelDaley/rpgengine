/**
 * @file joint_ik.c
 * @brief IK chain pair constraint row builder.
 *
 * Produces 3 angular Jacobian rows (one per world axis) that drive
 * two consecutive bodies in an IK chain to rotate the end-effector
 * toward a target position.
 *
 * Each row uses the lever arm from each body's center to the current
 * end-effector position.  The angular Jacobian cross-product naturally
 * weights bodies by their distance to the end-effector: upstream
 * bodies with longer lever arms produce larger torques per unit
 * angular velocity.
 *
 * Non-static functions: 1 (phys_joint_build_ik)
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>
#include <math.h>

/** Large clamp value for bilateral lambda bounds. */
#define JOINT_LAMBDA_BIG 1e10f

/**
 * @brief Build one angular IK row along a world axis.
 *
 * The Jacobian maps angular velocity of each body to end-effector
 * velocity along the given axis:
 *   v_ee_d = ω_a · (r_a × d) + ω_b · (r_b × d)
 *
 * where r_a, r_b are lever arms from each body to the end-effector,
 * and d is the constraint axis direction.
 *
 * @param row       Output Jacobian row.
 * @param r_a       Lever arm: ee_pos - body_a.position.
 * @param r_b       Lever arm: ee_pos - body_b.position.
 * @param axis      World-space constraint axis (unit vector).
 * @param error     End-effector positional error along this axis.
 * @param body_a    Body A (upstream).
 * @param body_b    Body B (downstream).
 * @param inv_i_a   World-space inverse inertia of body A.
 * @param inv_i_b   World-space inverse inertia of body B.
 * @param damping   Damping coefficient.
 */
static void build_ik_row(phys_jacobian_row_t *row,
                         phys_vec3_t r_a, phys_vec3_t r_b,
                         phys_vec3_t axis, float error,
                         const struct phys_body *body_a,
                         const struct phys_body *body_b,
                         const phys_mat3_t *inv_i_a,
                         const phys_mat3_t *inv_i_b,
                         float damping) {
    memset(row, 0, sizeof(*row));

    /* Angular-only: no linear Jacobian terms.
     * Positional coupling is handled by the existing ball/lock joints
     * between consecutive bones in the chain. */
    row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    /* Angular Jacobian:
     *   J_wa = -(r_a × axis)   (body_a rotation reduces error)
     *   J_wb =  (r_b × axis)   (body_b rotation reduces error) */
    row->J_wa = vec3_scale(vec3_cross(r_a, axis), -1.0f);
    row->J_wb = vec3_cross(r_b, axis);

    row->lambda_min = -JOINT_LAMBDA_BIG;
    row->lambda_max =  JOINT_LAMBDA_BIG;
    row->lambda     = 0.0f;

    row->bias    = error;
    row->damping = damping;

    row->effective_mass = phys_compute_effective_mass(
        row,
        body_a->inv_mass, inv_i_a,
        body_b->inv_mass, inv_i_b);
}

void phys_joint_build_ik(phys_joint_t *joint,
                         const struct phys_body *body_a,
                         const struct phys_body *body_b,
                         const struct phys_body *ee_body,
                         float dt) {
    if (!joint || !body_a || !body_b || !ee_body || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* End-effector position. */
    phys_vec3_t ee_pos = ee_body->position;

    /* Target position: read dynamically from target body if available,
     * otherwise use the static fallback. */
    phys_vec3_t target = joint->ik_target_pos;

    /* Error: how far the end-effector is from the target. */
    phys_vec3_t error = vec3_sub(ee_pos, target);

    /* Clamp large errors to prevent explosive corrections.
     * Beyond 2 meters of error, scale down linearly. */
    float err_len = sqrtf(vec3_dot(error, error));
    if (err_len > 2.0f) {
        error = vec3_scale(error, 2.0f / err_len);
    }

    /* Lever arms from each body center to the end-effector. */
    phys_vec3_t r_a = vec3_sub(ee_pos, body_a->position);
    phys_vec3_t r_b = vec3_sub(ee_pos, body_b->position);

    /* World-space inverse inertia tensors. */
    phys_mat3_t inv_i_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* IK damping: use stiffness field as influence weight.
     * Higher damping smooths convergence. */
    float ik_damping = joint->damping > 0.0f ? joint->damping : 5.0f;

    /* Three angular rows, one per world axis. */
    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    for (int i = 0; i < 3; ++i) {
        float axis_error = vec3_dot(error, axes[i]);

        /* Scale error by influence (stiffness field). */
        float influence = joint->spring_stiffness > 0.0f ? joint->spring_stiffness : 1.0f;
        axis_error *= influence;

        build_ik_row(&joint->rows[i], r_a, r_b, axes[i],
                     axis_error, body_a, body_b,
                     &inv_i_a, &inv_i_b, ik_damping);

        /* Warmstart from previous substep. */
        joint->rows[i].lambda = joint->cached_lambda[i];
    }

    joint->row_count = 3;
}
