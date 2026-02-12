/**
 * @file joint_hinge.c
 * @brief Hinge (revolute) joint constraint row builder.
 *
 * Produces 5 bilateral Jacobian rows:
 *   - Rows 0–2: positional lock along X, Y, Z (same as ball joint).
 *   - Rows 3–4: angular lock on two axes perpendicular to the hinge
 *     axis, preventing rotation around anything but the hinge.
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
    phys_vec3_t qv = {q.x, q.y, q.z};
    phys_vec3_t t = vec3_scale(vec3_cross(qv, v), 2.0f);
    return vec3_add(vec3_add(v, vec3_scale(t, q.w)), vec3_cross(qv, t));
}

/**
 * @brief Build one positional row along a world axis.
 */
static void build_positional_row(phys_jacobian_row_t *row,
                                 phys_vec3_t rA, phys_vec3_t rB,
                                 phys_vec3_t axis, float error,
                                 const struct phys_body *body_a,
                                 const struct phys_body *body_b) {
    memset(row, 0, sizeof(*row));

    row->J_va = vec3_scale(axis, -1.0f);
    row->J_wa = vec3_scale(vec3_cross(rA, axis), -1.0f);
    row->J_vb = axis;
    row->J_wb = vec3_cross(rB, axis);

    row->lambda_min = -JOINT_LAMBDA_BIG;
    row->lambda_max =  JOINT_LAMBDA_BIG;
    row->lambda = 0.0f;

    /* Position error stored in bias for split-impulse correction. */
    row->bias = error;

    row->effective_mass = phys_compute_effective_mass(
        row,
        body_a->inv_mass, &body_a->inv_inertia_diag,
        body_b->inv_mass, &body_b->inv_inertia_diag);
}

/**
 * @brief Compute an orthonormal basis perpendicular to an axis.
 *
 * Given a unit vector `axis`, produces two unit vectors `t1` and `t2`
 * that are mutually orthogonal and perpendicular to `axis`.
 */
static void compute_perp_basis(phys_vec3_t axis,
                               phys_vec3_t *t1,
                               phys_vec3_t *t2) {
    /* Choose a reference vector not parallel to axis. */
    phys_vec3_t ref;
    if (fabsf(axis.x) < 0.9f) {
        ref = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    } else {
        ref = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    }
    *t1 = vec3_normalize_safe(vec3_cross(axis, ref), 1e-8f);
    *t2 = vec3_cross(axis, *t1);
}

void phys_joint_build_hinge(phys_joint_t *joint,
                            const struct phys_body *body_a,
                            const struct phys_body *body_b,
                            float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* ── Positional rows (0–2): same as ball joint ──────────────── */

    phys_vec3_t world_anchor_a = vec3_add(
        body_a->position,
        quat_rotate_vec3(body_a->orientation, joint->local_anchor_a));
    phys_vec3_t world_anchor_b = vec3_add(
        body_b->position,
        quat_rotate_vec3(body_b->orientation, joint->local_anchor_b));

    phys_vec3_t pos_error = vec3_sub(world_anchor_b, world_anchor_a);
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    for (int i = 0; i < 3; ++i) {
        float axis_error = vec3_dot(pos_error, axes[i]);
        build_positional_row(&joint->rows[i], rA, rB, axes[i],
                             axis_error, body_a, body_b);
    }

    /* ── Angular rows (3–4): lock rotation off the hinge axis ───── */

    /* Transform the hinge axis from body A's local space to world. */
    phys_vec3_t world_axis = vec3_normalize_safe(
        quat_rotate_vec3(body_a->orientation, joint->local_axis_a), 1e-8f);

    /* Compute two directions perpendicular to the hinge axis. */
    phys_vec3_t perp1, perp2;
    compute_perp_basis(world_axis, &perp1, &perp2);

    /* Angular-only rows: linear Jacobians are zero, angular Jacobians
     * constrain relative angular velocity to lie along the hinge axis.
     * J_wa = -perp, J_wb = +perp. */
    for (int k = 0; k < 2; ++k) {
        phys_jacobian_row_t *row = &joint->rows[3 + k];
        memset(row, 0, sizeof(*row));

        phys_vec3_t perp = (k == 0) ? perp1 : perp2;

        row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_wa = vec3_scale(perp, -1.0f);
        row->J_wb = perp;

        row->lambda_min = -JOINT_LAMBDA_BIG;
        row->lambda_max =  JOINT_LAMBDA_BIG;
        row->lambda = 0.0f;
        row->bias = 0.0f;  /* No angular drift correction for now. */

        row->effective_mass = phys_compute_effective_mass(
            row,
            body_a->inv_mass, &body_a->inv_inertia_diag,
            body_b->inv_mass, &body_b->inv_inertia_diag);
    }

    joint->row_count = 5;
}
