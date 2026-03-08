/**
 * @file joint_limit_position.c
 * @brief Build Jacobian rows for per-axis positional limits.
 *
 * Produces up to 3 one-sided positional rows, one per axis enabled
 * in joint->limit_axes.  Each row activates only when the relative
 * position exceeds limit_min or limit_max on that axis.  Maps to
 * Limit Location and Floor animation constraints.
 *
 * Non-static functions: 1 (phys_joint_build_limit_position)
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <string.h>

#define JOINT_LAMBDA_BIG 1e10f

void phys_joint_build_limit_position(phys_joint_t *joint,
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

    /* Positional offset in body_a's local frame for limit comparison. */
    phys_vec3_t delta_world = vec3_sub(world_anchor_b, world_anchor_a);
    phys_vec3_t delta_local = quat_inv_rotate_vec3(
        body_a->orientation, delta_world);

    /* Lever arms. */
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    phys_mat3_t inv_i_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    /* Per-axis world directions from body_a's frame. */
    phys_vec3_t axes[3];
    axes[0] = quat_rotate_vec3(body_a->orientation, (phys_vec3_t){1,0,0});
    axes[1] = quat_rotate_vec3(body_a->orientation, (phys_vec3_t){0,1,0});
    axes[2] = quat_rotate_vec3(body_a->orientation, (phys_vec3_t){0,0,1});

    float local_vals[3] = {delta_local.x, delta_local.y, delta_local.z};

    uint8_t rc = 0;
    for (int i = 0; i < 3; ++i) {
        if (!(joint->limit_axes & (1u << i))) continue;

        float pos = local_vals[i];
        float lo  = joint->limit_min[i];
        float hi  = joint->limit_max[i];

        float error = 0.0f;
        float lmin = 0.0f, lmax = 0.0f;
        if (pos < lo) {
            error = pos - lo;
            lmin = -JOINT_LAMBDA_BIG;
            lmax = 0.0f;
        } else if (pos > hi) {
            error = pos - hi;
            lmin = 0.0f;
            lmax = JOINT_LAMBDA_BIG;
        } else {
            continue;
        }

        phys_jacobian_row_t *row = &joint->rows[rc];
        memset(row, 0, sizeof(*row));
        row->J_va = vec3_scale(axes[i], -1.0f);
        row->J_wa = vec3_scale(vec3_cross(rA, axes[i]), -1.0f);
        row->J_vb = axes[i];
        row->J_wb = vec3_cross(rB, axes[i]);
        row->lambda_min = lmin;
        row->lambda_max = lmax;
        /* Limit rows are data-dependent and can reorder as different axes
         * clamp, so avoid warmstarting them by transient row index. */
        row->lambda = 0.0f;
        row->bias = error;
        row->damping = joint->damping;
        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_a,
            body_b->inv_mass, &inv_i_b);
        rc++;
    }

    joint->row_count = rc;
}
