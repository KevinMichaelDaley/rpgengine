/**
 * @file limit_rotation.c
 * @brief Limit Rotation constraint evaluator.
 *
 * Clamps Euler angles (XYZ intrinsic) to specified min/max ranges.
 * Maps directly to physics joint angle limits.
 *
 * Non-static functions: 1 (eval_limit_rotation)
 */

#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include <math.h>

/**
 * @brief Clamp a value to [min, max].
 */
static float clampf_(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/**
 * @brief Extract XYZ intrinsic Euler angles from a 3x3 rotation matrix.
 *
 * Assumes the upper-left 3x3 of a column-major mat4 is a pure rotation
 * (normalized columns). Uses the convention:
 *   R = Rz(z) * Ry(y) * Rx(x)
 *
 * Which gives:
 *   m[0] = cy*cz,  m[4] = sx*sy*cz - cx*sz,  m[8]  = cx*sy*cz + sx*sz
 *   m[1] = cy*sz,  m[5] = sx*sy*sz + cx*cz,  m[9]  = cx*sy*sz - sx*cz
 *   m[2] = -sy,    m[6] = sx*cy,              m[10] = cx*cy
 */
static void mat4_to_euler_xyz_(const mat4_t *m, float *x, float *y, float *z) {
    /* Normalize columns to remove scale. */
    float c0_len = sqrtf(m->m[0]*m->m[0] + m->m[1]*m->m[1] + m->m[2]*m->m[2]);
    float c1_len = sqrtf(m->m[4]*m->m[4] + m->m[5]*m->m[5] + m->m[6]*m->m[6]);
    float c2_len = sqrtf(m->m[8]*m->m[8] + m->m[9]*m->m[9] + m->m[10]*m->m[10]);
    if (c0_len < 1e-7f) c0_len = 1.0f;
    if (c1_len < 1e-7f) c1_len = 1.0f;
    if (c2_len < 1e-7f) c2_len = 1.0f;

    float r00 = m->m[0] / c0_len;
    float r10 = m->m[1] / c0_len;
    float r20 = m->m[2] / c0_len;
    float r21 = m->m[6] / c1_len;
    float r22 = m->m[10] / c2_len;

    float sy = -r20;
    if (sy > 1.0f) sy = 1.0f;
    if (sy < -1.0f) sy = -1.0f;
    *y = asinf(sy);

    float cy = cosf(*y);
    if (fabsf(cy) > 1e-6f) {
        *x = atan2f(r21, r22);
        *z = atan2f(r10, r00);
    } else {
        /* Gimbal lock: set x=0, solve z. */
        *x = 0.0f;
        *z = atan2f(-m->m[4] / c1_len, m->m[5] / c1_len);
    }
}

/**
 * @brief Limit Rotation evaluator.
 *
 * Extracts Euler angles, clamps them per-axis, and rebuilds the rotation.
 * Preserves translation and scale.
 */
void eval_limit_rotation(const constraint_def_t *def,
                         const constraint_eval_ctx_t *ctx,
                         mat4_t *inout) {
    (void)ctx;
    if (!def || !inout) return;

    const constraint_limit_rotation_params_t *p = &def->params.limit_rotation;
    if (!p->use_limit_x && !p->use_limit_y && !p->use_limit_z) return;

    /* Extract Euler angles. */
    float ex, ey, ez;
    mat4_to_euler_xyz_(inout, &ex, &ey, &ez);

    bool changed = false;

    if (p->use_limit_x) {
        float clamped = clampf_(ex, p->min_x, p->max_x);
        if (clamped != ex) { ex = clamped; changed = true; }
    }
    if (p->use_limit_y) {
        float clamped = clampf_(ey, p->min_y, p->max_y);
        if (clamped != ey) { ey = clamped; changed = true; }
    }
    if (p->use_limit_z) {
        float clamped = clampf_(ez, p->min_z, p->max_z);
        if (clamped != ez) { ez = clamped; changed = true; }
    }

    if (!changed) return;

    /* Extract scale. */
    float scale[3];
    for (int col = 0; col < 3; col++) {
        float x = inout->m[col * 4 + 0];
        float y = inout->m[col * 4 + 1];
        float z = inout->m[col * 4 + 2];
        scale[col] = sqrtf(x * x + y * y + z * z);
        if (scale[col] < 1e-7f) scale[col] = 1.0f;
    }

    /* Preserve translation. */
    float tx = inout->m[12], ty = inout->m[13], tz = inout->m[14];

    /* Rebuild rotation from clamped Euler angles. */
    quat_t q = quat_from_euler(ex, ey, ez);
    mat4_t rot;
    quat_to_mat4(q, &rot);

    /* Apply scale and translation. */
    for (int col = 0; col < 3; col++) {
        inout->m[col * 4 + 0] = rot.m[col * 4 + 0] * scale[col];
        inout->m[col * 4 + 1] = rot.m[col * 4 + 1] * scale[col];
        inout->m[col * 4 + 2] = rot.m[col * 4 + 2] * scale[col];
    }
    inout->m[12] = tx;
    inout->m[13] = ty;
    inout->m[14] = tz;
}
