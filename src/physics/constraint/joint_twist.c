/**
 * @file joint_twist.c
 * @brief Single-axis twist joint constraint row builder.
 *
 * Produces 5 or 6 bilateral Jacobian rows:
 *   - Rows 0–2: positional lock along X, Y, Z (anchor coincidence).
 *   - Rows 3–4: angular lock on two axes perpendicular to the twist
 *     axis, preventing rotation around anything but the twist axis.
 *   - Row 5 (optional): twist angle limit when limit_axes bit 0 is set.
 *
 * Structurally identical to a hinge joint but with optional angular
 * limits on the free axis, using swing-twist decomposition from the
 * rest_relative_orient reference.
 *
 * Non-static functions: 1 (phys_joint_build_twist)
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
 * @brief Build a bilateral positional row along a world axis.
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
    row->lambda = 0.0f;
    row->bias = error;
    row->constraint_error = error;
    row->damping = row_damping;
    row->effective_mass = phys_compute_effective_mass(
        row, body_a->inv_mass, inv_i_world_a,
        body_b->inv_mass, inv_i_world_b);
}

/**
 * @brief Compute an orthonormal basis perpendicular to an axis.
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

void phys_joint_build_twist(phys_joint_t *joint,
                             const struct phys_body *body_a,
                             const struct phys_body *body_b,
                             float dt) {
    if (!joint || !body_a || !body_b || dt <= 0.0f) {
        if (joint) { joint->row_count = 0; }
        return;
    }

    /* ── Positional rows (0–2): anchor coincidence ────────────────── */

    phys_vec3_t world_anchor_a = vec3_add(
        body_a->position,
        quat_rotate_vec3(body_a->orientation, joint->local_anchor_a));
    phys_vec3_t world_anchor_b = vec3_add(
        body_b->position,
        quat_rotate_vec3(body_b->orientation, joint->local_anchor_b));

    phys_vec3_t pos_error = vec3_sub(world_anchor_b, world_anchor_a);
    phys_vec3_t rA = vec3_sub(world_anchor_a, body_a->position);
    phys_vec3_t rB = vec3_sub(world_anchor_b, body_b->position);

    phys_mat3_t inv_i_world_a = phys_mat3_inv_inertia_world(
        body_a->orientation, body_a->inv_inertia_diag);
    phys_mat3_t inv_i_world_b = phys_mat3_inv_inertia_world(
        body_b->orientation, body_b->inv_inertia_diag);

    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    for (int i = 0; i < 3; ++i) {
        float axis_error = vec3_dot(pos_error, axes[i]);
        build_positional_row(&joint->rows[i], rA, rB, axes[i],
                             axis_error, body_a, body_b,
                             &inv_i_world_a, &inv_i_world_b,
                             joint->damping);
        joint->rows[i].lambda = joint->cached_lambda[i];
    }

    /* ── Angular rows (3–4): lock rotation off the twist axis ────── */

    /* Transform the twist axis from body A's local space to world. */
    phys_vec3_t world_axis = vec3_normalize_safe(
        quat_rotate_vec3(body_a->orientation, joint->local_axis_a), 1e-8f);

    /* Two directions perpendicular to the twist axis. */
    phys_vec3_t perp1, perp2;
    compute_perp_basis(world_axis, &perp1, &perp2);

    /* Compute angular error: how much body B's twist axis deviates
     * from body A's.  Project body B's axis onto the two perp directions. */
    phys_vec3_t world_axis_b = vec3_normalize_safe(
        quat_rotate_vec3(body_b->orientation, joint->local_axis_a), 1e-8f);

    float perp_errors[2] = {
        vec3_dot(world_axis_b, perp1) - vec3_dot(world_axis, perp1),
        vec3_dot(world_axis_b, perp2) - vec3_dot(world_axis, perp2),
    };

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
        row->lambda = joint->cached_lambda[3 + k];
        row->bias = perp_errors[k];
        row->constraint_error = perp_errors[k];
        row->damping = joint->damping;
        row->flags = PHYS_ROW_FLAG_ANGULAR;

        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_world_a,
            body_b->inv_mass, &inv_i_world_b);
    }

    uint8_t rc = 5;

    /* ── Optional twist limit row (5) ─────────────────────────────── */

    if (joint->limit_axes & 0x1) {
        /* Compute twist angle from relative orientation error. */
        phys_quat_t q_current = quat_normalize_safe(
            quat_mul(body_b->orientation,
                     quat_conjugate(body_a->orientation)),
            1e-12f);
        phys_quat_t q_error = quat_normalize_safe(
            quat_mul(quat_conjugate(joint->rest_relative_orient),
                     q_current),
            1e-12f);

        /* Ensure shortest path. */
        if (q_error.w < 0.0f) {
            q_error.x = -q_error.x; q_error.y = -q_error.y;
            q_error.z = -q_error.z; q_error.w = -q_error.w;
        }

        /* Extract twist angle: project error quaternion onto twist axis.
         * q_twist = normalize(w, x, 0, 0) for X-axis twist. */
        float tw = q_error.w, tx = q_error.x;
        float twist_len = sqrtf(tw * tw + tx * tx);
        if (twist_len > 1e-8f) {
            tw /= twist_len;
            tx /= twist_len;
        }
        float twist_angle = 2.0f * atan2f(tx, tw);

        float lo = joint->limit_min[0];
        float hi = joint->limit_max[0];

        float ang_error = 0.0f;
        float lmin = -JOINT_LAMBDA_BIG;
        float lmax =  JOINT_LAMBDA_BIG;

        if (twist_angle < lo) {
            ang_error = twist_angle - lo;
            lmin = 0.0f;
            lmax = JOINT_LAMBDA_BIG;
        } else if (twist_angle > hi) {
            ang_error = twist_angle - hi;
            lmin = -JOINT_LAMBDA_BIG;
            lmax = 0.0f;
        }
        /* Within limits: bilateral speculative row with zero bias. */

        phys_jacobian_row_t *row = &joint->rows[rc];
        memset(row, 0, sizeof(*row));
        row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_wa = vec3_scale(world_axis, -1.0f);
        row->J_wb = world_axis;
        row->lambda_min = lmin;
        row->lambda_max = lmax;
        row->lambda = joint->cached_lambda[rc];
        row->bias = ang_error;
        row->constraint_error = ang_error;
        row->damping = joint->damping;
        row->flags = PHYS_ROW_FLAG_ANGULAR;

        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_world_a,
            body_b->inv_mass, &inv_i_world_b);
        rc++;
    }

    joint->row_count = rc;
}
