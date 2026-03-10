/**
 * @file joint_copy_rotation.c
 * @brief Build Jacobian rows for a copy-rotation joint.
 *
 * Produces 3 bilateral angular rows that drive body_b's orientation
 * to match body_a's orientation.  No positional constraint is applied.
 * Maps to the Copy Rotation animation constraint.
 *
 * Non-static functions: 1 (phys_joint_build_copy_rotation)
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>

#define JOINT_LAMBDA_BIG 1e10f

void phys_joint_build_copy_rotation(phys_joint_t *joint,
                                    const struct phys_body *body_a,
                                    const struct phys_body *body_b,
                                    float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    phys_mat3_t inv_i_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* Relative rotation: q_rel = q_b * conjugate(q_a).
     * For matching orientations, q_rel should be identity. */
    phys_quat_t q_rel = quat_normalize_safe(
        quat_mul(body_b->orientation, quat_conjugate(body_a->orientation)),
        1e-12f);
    if (q_rel.w < 0.0f) {
        q_rel.x = -q_rel.x; q_rel.y = -q_rel.y;
        q_rel.z = -q_rel.z; q_rel.w = -q_rel.w;
    }

    /* Angular error ≈ 2 * vec(q_rel). */
    phys_vec3_t ang_err = {2.0f * q_rel.x, 2.0f * q_rel.y, 2.0f * q_rel.z};

    /* Three angular rows: one per world axis. */
    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    for (int i = 0; i < 3; ++i) {
        phys_jacobian_row_t *row = &joint->rows[i];
        memset(row, 0, sizeof(*row));

        row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_wa = vec3_scale(axes[i], -1.0f);
        row->J_wb = axes[i];

        row->lambda_min = -JOINT_LAMBDA_BIG;
        row->lambda_max =  JOINT_LAMBDA_BIG;
        row->lambda = joint->cached_lambda[i];
        row->bias = vec3_dot(ang_err, axes[i]);
        row->constraint_error = vec3_dot(ang_err, axes[i]);
        row->damping = joint->damping;
        row->flags = PHYS_ROW_FLAG_ANGULAR;

        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_a,
            body_b->inv_mass, &inv_i_b);
    }

    joint->row_count = 3;
}
