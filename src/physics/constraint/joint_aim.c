/**
 * @file joint_aim.c
 * @brief Build Jacobian rows for an aim (track-to) joint.
 *
 * Produces 2 angular rows that align body_b's track_axis toward
 * body_a's position.  The two rows are perpendicular to the desired
 * aim direction, preventing body_b from rotating off the aim vector.
 * Maps to Damped Track / Track To / Locked Track animation constraints.
 *
 * Non-static functions: 1 (phys_joint_build_aim)
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
 * @brief Compute two vectors perpendicular to a given axis.
 */
static void compute_perp_basis(phys_vec3_t axis,
                               phys_vec3_t *t1,
                               phys_vec3_t *t2) {
    phys_vec3_t ref;
    if (fabsf(axis.x) < 0.9f) {
        ref = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    } else {
        ref = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    }
    *t1 = vec3_normalize_safe(vec3_cross(axis, ref), 1e-8f);
    *t2 = vec3_cross(axis, *t1);
}

void phys_joint_build_aim(phys_joint_t *joint,
                          const struct phys_body *body_a,
                          const struct phys_body *body_b,
                          float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* Direction from body_b to body_a in world space. */
    phys_vec3_t to_target = vec3_sub(body_a->position, body_b->position);
    float dist = sqrtf(vec3_dot(to_target, to_target));
    if (dist < 1e-6f) {
        /* Target is coincident — no aim direction definable. */
        joint->row_count = 0;
        return;
    }
    phys_vec3_t desired_dir = vec3_scale(to_target, 1.0f / dist);

    /* Current aim direction: body_b's track_axis in world space. */
    phys_vec3_t track = joint->track_axis;
    float track_len = sqrtf(vec3_dot(track, track));
    if (track_len < 1e-6f) {
        track = (phys_vec3_t){0.0f, 1.0f, 0.0f};  /* Default: +Y. */
    }
    phys_vec3_t current_dir = vec3_normalize_safe(
        quat_rotate_vec3(body_b->orientation, track), 1e-8f);

    /* Angular error: cross product gives the rotation axis and sin(angle).
     * For small errors, cross ≈ axis * angle. */
    phys_vec3_t error_axis = vec3_cross(current_dir, desired_dir);

    /* Two perpendicular constraint axes (avoid constraining roll). */
    phys_vec3_t perp1, perp2;
    compute_perp_basis(desired_dir, &perp1, &perp2);

    phys_mat3_t inv_i_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* Row 0: error projected onto perp1. */
    /* Row 1: error projected onto perp2. */
    for (int k = 0; k < 2; ++k) {
        phys_vec3_t perp = (k == 0) ? perp1 : perp2;
        float err = vec3_dot(error_axis, perp);

        phys_jacobian_row_t *row = &joint->rows[k];
        memset(row, 0, sizeof(*row));
        row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_wa = vec3_scale(perp, -1.0f);
        row->J_wb = perp;
        row->lambda_min = -JOINT_LAMBDA_BIG;
        row->lambda_max =  JOINT_LAMBDA_BIG;
        row->lambda = joint->cached_lambda[k];
        row->bias = err;
        row->damping = joint->damping;
        row->flags = PHYS_ROW_FLAG_ANGULAR;
        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_a,
            body_b->inv_mass, &inv_i_b);
    }

    joint->row_count = 2;
}
