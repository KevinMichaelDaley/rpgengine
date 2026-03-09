/**
 * @file joint_distance.c
 * @brief Distance joint constraint row builder.
 *
 * Produces 1 bilateral Jacobian row along the anchor separation axis.
 * Bias corrects the difference between current anchor distance and
 * the configured rest_length.
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_mat3.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <string.h>

/** Large clamp value for bilateral lambda bounds. */
#define JOINT_LAMBDA_BIG 1e10f



void phys_joint_init(phys_joint_t *joint) {
    if (!joint) { return; }
    memset(joint, 0, sizeof(*joint));
    joint->type = PHYS_JOINT_DISTANCE;
    joint->body_a = UINT32_MAX;
    joint->body_b = UINT32_MAX;
    joint->ik_target_body = UINT32_MAX;
    joint->rest_relative_orient = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    joint->mass_scale = 10.0f;
}

void phys_joint_build_distance(phys_joint_t *joint,
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

    /* Separation vector and current distance. */
    phys_vec3_t delta = vec3_sub(world_anchor_b, world_anchor_a);
    float dist = sqrtf(vec3_dot(delta, delta));

    /* Range mode: when limit_axes bit 0 is set, limit_min[0] and
     * limit_max[0] define the allowed distance range.  Within the
     * range the constraint is inactive (row_count=0).  Outside, a
     * unilateral row pushes/pulls back to the nearest bound —
     * similar to a contact constraint but for distance. */
    float error;
    float lam_min, lam_max;
    if (joint->limit_axes & 1) {
        float min_dist = joint->limit_min[0];
        float max_dist = joint->limit_max[0];
        if (dist < min_dist) {
            /* Too close — push apart. */
            error = dist - min_dist;        /* negative */
            lam_min = 0.0f;                 /* repulsive only */
            lam_max = JOINT_LAMBDA_BIG;
        } else if (dist > max_dist) {
            /* Too far — pull together. */
            error = dist - max_dist;        /* positive */
            lam_min = -JOINT_LAMBDA_BIG;    /* attractive only */
            lam_max = 0.0f;
        } else {
            /* Within allowed range — no constraint. */
            joint->row_count = 0;
            return;
        }
    } else {
        /* Exact distance mode (bilateral). */
        error = dist - joint->rest_length;
        lam_min = -JOINT_LAMBDA_BIG;
        lam_max =  JOINT_LAMBDA_BIG;
    }

    /* Constraint direction: normalized separation (or fallback X axis). */
    phys_vec3_t dir;
    if (dist > 1e-7f) {
        dir = vec3_scale(delta, 1.0f / dist);
    } else {
        dir = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    }

    /* Lever arms from body centers to world anchors. */
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    /* Build the single Jacobian row. */
    phys_jacobian_row_t *row = &joint->rows[0];
    memset(row, 0, sizeof(*row));

    row->J_va = vec3_scale(dir, -1.0f);
    row->J_wa = vec3_scale(vec3_cross(rA, dir), -1.0f);
    row->J_vb = dir;
    row->J_wb = vec3_cross(rB, dir);

    row->lambda_min = lam_min;
    row->lambda_max = lam_max;
    row->lambda = joint->cached_lambda[0]; /* Warmstart from previous substep. */

    /* Position error stored in bias for split-impulse correction.
     * The velocity-level solve sees bias=0 (set by the solver);
     * position correction uses this raw error value. */
    row->bias = error;
    row->damping = joint->damping;

    /* Effective mass (world-space inverse inertia). */
    phys_mat3_t inv_i_world_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_world_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);
    row->effective_mass = phys_compute_effective_mass(
        row,
        body_a->inv_mass, &inv_i_world_a,
        body_b->inv_mass, &inv_i_world_b);

    joint->row_count = 1;
}
