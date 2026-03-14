/**
 * @file quat_euler_yxz.c
 * @brief Quaternion ↔ Euler conversion in YXZ matrix multiplication order.
 *
 * The engine builds entity rotation matrices as R = Ry * Rx * Rz.
 * This file provides conversions between that euler convention and
 * quaternions, enabling correct rotation composition.
 *
 * Non-static functions: 2 (quat_from_euler_yxz, quat_to_euler_yxz).
 */

#include "ferrum/math/quat.h"
#include <math.h>

quat_t quat_from_euler_yxz(float x, float y, float z) {
    float cx = cosf(x * 0.5f), sx = sinf(x * 0.5f);
    float cy = cosf(y * 0.5f), sy = sinf(y * 0.5f);
    float cz = cosf(z * 0.5f), sz = sinf(z * 0.5f);

    /* q = qy * qx * qz (matching R = Ry * Rx * Rz matrix order). */
    return (quat_t){
        .x = cy * sx * cz + sy * cx * sz,
        .y = sy * cx * cz - cy * sx * sz,
        .z = cy * cx * sz - sy * sx * cz,
        .w = cy * cx * cz + sy * sx * sz,
    };
}

void quat_to_euler_yxz(quat_t q, float *x, float *y, float *z) {
    /* Convert quaternion to rotation matrix elements needed for extraction.
     * R = Ry * Rx * Rz gives:
     *   R[1][2] = -sin(x)
     *   R[0][2] =  cos(x)*sin(y)
     *   R[2][2] =  cos(x)*cos(y)
     *   R[1][0] =  cos(x)*sin(z)
     *   R[1][1] =  cos(x)*cos(z)
     *
     * Matrix element R[row][col] in column-major = m[col*4 + row].
     */
    mat4_t m;
    quat_to_mat4(q, &m);

    /* R[1][2] = m[2*4+1] = m[9] = -sin(x). */
    float sin_x = -m.m[9];

    /* Clamp to [-1, 1] to avoid NaN from asinf. */
    if (sin_x > 1.0f) sin_x = 1.0f;
    if (sin_x < -1.0f) sin_x = -1.0f;

    *x = asinf(sin_x);

    float cos_x = cosf(*x);
    if (fabsf(cos_x) > 1e-6f) {
        /* Normal case: extract Y and Z from matrix. */
        /* R[0][2] = m[8], R[2][2] = m[10]. */
        *y = atan2f(m.m[8], m.m[10]);
        /* R[1][0] = m[1], R[1][1] = m[5]. */
        *z = atan2f(m.m[1], m.m[5]);
    } else {
        /* Gimbal lock: x ≈ ±90°. Z is indeterminate; set to 0. */
        *z = 0.0f;
        /* R[2][0] = m[2], R[0][0] = m[0]. */
        *y = atan2f(-m.m[2], m.m[0]);
    }
}
