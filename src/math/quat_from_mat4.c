/**
 * @file quat_from_mat4.c
 * @brief Extract rotation quaternion from a 4×4 column-major matrix.
 *
 * Strips scale from the upper-left 3×3 and converts to quaternion
 * using Shepperd's method (largest-diagonal selection).
 *
 * Non-static functions: 1 (quat_from_mat4)
 */

#include "ferrum/math/quat.h"
#include <math.h>

quat_t quat_from_mat4(const mat4_t *m) {
    /* Extract column magnitudes (scale). */
    float sx = sqrtf(m->m[0]*m->m[0] + m->m[1]*m->m[1] + m->m[2]*m->m[2]);
    float sy = sqrtf(m->m[4]*m->m[4] + m->m[5]*m->m[5] + m->m[6]*m->m[6]);
    float sz = sqrtf(m->m[8]*m->m[8] + m->m[9]*m->m[9] + m->m[10]*m->m[10]);
    if (sx < 1e-7f) sx = 1e-7f;
    if (sy < 1e-7f) sy = 1e-7f;
    if (sz < 1e-7f) sz = 1e-7f;

    /* Normalized rotation matrix elements (column-major). */
    float r00 = m->m[0] / sx, r01 = m->m[4] / sy, r02 = m->m[8]  / sz;
    float r10 = m->m[1] / sx, r11 = m->m[5] / sy, r12 = m->m[9]  / sz;
    float r20 = m->m[2] / sx, r21 = m->m[6] / sy, r22 = m->m[10] / sz;

    float trace = r00 + r11 + r22;
    quat_t q;
    if (trace > 0.f) {
        float s = 0.5f / sqrtf(trace + 1.f);
        q.w = 0.25f / s;
        q.x = (r21 - r12) * s;
        q.y = (r02 - r20) * s;
        q.z = (r10 - r01) * s;
    } else if (r00 > r11 && r00 > r22) {
        float s = 2.f * sqrtf(1.f + r00 - r11 - r22);
        q.w = (r21 - r12) / s;
        q.x = 0.25f * s;
        q.y = (r01 + r10) / s;
        q.z = (r02 + r20) / s;
    } else if (r11 > r22) {
        float s = 2.f * sqrtf(1.f + r11 - r00 - r22);
        q.w = (r02 - r20) / s;
        q.x = (r01 + r10) / s;
        q.y = 0.25f * s;
        q.z = (r12 + r21) / s;
    } else {
        float s = 2.f * sqrtf(1.f + r22 - r00 - r11);
        q.w = (r10 - r01) / s;
        q.x = (r02 + r20) / s;
        q.y = (r12 + r21) / s;
        q.z = 0.25f * s;
    }

    /* Normalize. */
    float len = sqrtf(q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w);
    if (len > 1e-7f) {
        float inv = 1.f / len;
        q.x *= inv; q.y *= inv; q.z *= inv; q.w *= inv;
    }
    return q;
}
