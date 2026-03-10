/**
 * @file joint_limit_rotation.c
 * @brief Build Jacobian rows for per-axis angular limits.
 *
 * Produces up to 3 one-sided angular rows, one per axis enabled in
 * joint->limit_axes.  Each row activates only when the relative angle
 * exceeds limit_min or limit_max on that axis.  This maps to the
 * Limit Rotation animation constraint.
 *
 * Non-static functions: 1 (phys_joint_build_limit_rotation)
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
 * @brief Extract approximate Euler angle for one axis from a quaternion.
 *
 * Uses the small-angle approximation (2 * vec component) which is
 * stable for angles up to ~60°.  For larger angles, extracts the
 * full atan2-based Euler angle.
 */
static float extract_axis_angle(phys_quat_t q, int axis) {
    /* Full Euler extraction for better accuracy at large angles. */
    float x = q.x, y = q.y, z = q.z, w = q.w;
    switch (axis) {
    case 0: /* X: atan2(2(wy+xz), 1-2(x²+y²)) — roll */
        return atan2f(2.0f * (w * x + y * z),
                      1.0f - 2.0f * (x * x + y * y));
    case 1: { /* Y: asin(2(wy-xz)) — pitch */
        float sinp = 2.0f * (w * y - z * x);
        if (sinp >  1.0f) sinp =  1.0f;
        if (sinp < -1.0f) sinp = -1.0f;
        return asinf(sinp);
    }
    case 2: /* Z: atan2(2(wz+xy), 1-2(y²+z²)) — yaw */
        return atan2f(2.0f * (w * z + x * y),
                      1.0f - 2.0f * (y * y + z * z));
    default:
        return 0.0f;
    }
}

void phys_joint_build_limit_rotation(phys_joint_t *joint,
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

    /* Relative rotation: q_rel = q_b * conjugate(q_a). */
    phys_quat_t q_rel = quat_normalize_safe(
        quat_mul(body_b->orientation, quat_conjugate(body_a->orientation)),
        1e-12f);
    if (q_rel.w < 0.0f) {
        q_rel.x = -q_rel.x; q_rel.y = -q_rel.y;
        q_rel.z = -q_rel.z; q_rel.w = -q_rel.w;
    }

    static const phys_vec3_t axes[3] = {
        {1.0f, 0.0f, 0.0f},
        {0.0f, 1.0f, 0.0f},
        {0.0f, 0.0f, 1.0f},
    };

    uint8_t rc = 0;
    for (int i = 0; i < 3; ++i) {
        if (!(joint->limit_axes & (1u << i))) continue;

        float angle = extract_axis_angle(q_rel, i);
        float lo = joint->limit_min[i];
        float hi = joint->limit_max[i];

        /* Determine if limit is violated and compute error + bounds. */
        float error = 0.0f;
        float lmin = 0.0f, lmax = 0.0f;
        if (angle < lo) {
            error = angle - lo;          /* Negative: need positive λ. */
            lmin = 0.0f;
            lmax = JOINT_LAMBDA_BIG;
        } else if (angle > hi) {
            error = angle - hi;          /* Positive: need negative λ. */
            lmin = -JOINT_LAMBDA_BIG;
            lmax = 0.0f;
        } else {
            continue;  /* Within limits — no constraint needed. */
        }

        phys_jacobian_row_t *row = &joint->rows[rc];
        memset(row, 0, sizeof(*row));
        row->J_va = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_vb = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        row->J_wa = vec3_scale(axes[i], -1.0f);
        row->J_wb = axes[i];
        row->lambda_min = lmin;
        row->lambda_max = lmax;
        /* Row activation changes with the violated axes, so keep limit
         * rows cold-started instead of reusing a mismatched lambda. */
        row->lambda = 0.0f;
        row->bias = error;
        row->constraint_error = error;
        row->damping = joint->damping;
        row->flags = PHYS_ROW_FLAG_ANGULAR;
        row->effective_mass = phys_compute_effective_mass(
            row, body_a->inv_mass, &inv_i_a,
            body_b->inv_mass, &inv_i_b);
        rc++;
    }

    joint->row_count = rc;
}
