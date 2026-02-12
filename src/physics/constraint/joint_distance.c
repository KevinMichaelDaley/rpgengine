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
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <string.h>

/** Large clamp value for bilateral lambda bounds. */
#define JOINT_LAMBDA_BIG 1e10f



/**
 * @brief Rotate a vector by a quaternion: q * v * q^-1.
 */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v) {
    /* t = 2 * cross(q.xyz, v) */
    phys_vec3_t qv = {q.x, q.y, q.z};
    phys_vec3_t t = vec3_scale(vec3_cross(qv, v), 2.0f);
    /* result = v + w*t + cross(q.xyz, t) */
    return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}

void phys_joint_init(phys_joint_t *joint) {
    if (!joint) { return; }
    memset(joint, 0, sizeof(*joint));
    joint->type = PHYS_JOINT_DISTANCE;
    joint->body_a = UINT32_MAX;
    joint->body_b = UINT32_MAX;
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

    /* Bilateral lambda bounds (can push and pull). */
    row->lambda_min = -JOINT_LAMBDA_BIG;
    row->lambda_max =  JOINT_LAMBDA_BIG;
    row->lambda = 0.0f;

    /* Position error stored in bias for split-impulse correction.
     * The velocity-level solve sees bias=0 (set by the solver);
     * position correction uses this raw error value. */
    float error = dist - joint->rest_length;
    row->bias = error;

    /* Effective mass. */
    row->effective_mass = phys_compute_effective_mass(
        row,
        body_a->inv_mass, &body_a->inv_inertia_diag,
        body_b->inv_mass, &body_b->inv_inertia_diag);

    joint->row_count = 1;
}
