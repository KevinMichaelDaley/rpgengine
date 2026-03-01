/**
 * @file aegis_ops_quat.c
 * @brief Quaternion ops: quat_mul, quat_rotate.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * Quaternion layout: vec4[4] = {x, y, z, w}.
 */

#include "ferrum/aegis/aegis_ops_math.h"
#include <string.h>

void aegis_op_quat_mul(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b) {
    /* Hamilton product: q = a * b
     * Layout: [x, y, z, w] */
    float ax = a->vec4[0], ay = a->vec4[1], az = a->vec4[2], aw = a->vec4[3];
    float bx = b->vec4[0], by = b->vec4[1], bz = b->vec4[2], bw = b->vec4[3];

    memset(dst, 0, sizeof(*dst));
    dst->vec4[0] = aw * bx + ax * bw + ay * bz - az * by;
    dst->vec4[1] = aw * by - ax * bz + ay * bw + az * bx;
    dst->vec4[2] = aw * bz + ax * by - ay * bx + az * bw;
    dst->vec4[3] = aw * bw - ax * bx - ay * by - az * bz;
}

void aegis_op_quat_rotate(aegis_register_t *dst,
                           const aegis_register_t *quat,
                           const aegis_register_t *vec) {
    /* Rotate vec3 by quaternion: v' = q * v * q^-1
     * Optimized formula: v' = v + 2 * cross(q.xyz, cross(q.xyz, v) + q.w * v) */
    float qx = quat->vec4[0], qy = quat->vec4[1], qz = quat->vec4[2], qw = quat->vec4[3];
    float vx = vec->vec3[0], vy = vec->vec3[1], vz = vec->vec3[2];

    /* t = 2 * cross(q.xyz, v) */
    float tx = 2.0f * (qy * vz - qz * vy);
    float ty = 2.0f * (qz * vx - qx * vz);
    float tz = 2.0f * (qx * vy - qy * vx);

    memset(dst, 0, sizeof(*dst));
    dst->vec3[0] = vx + qw * tx + (qy * tz - qz * ty);
    dst->vec3[1] = vy + qw * ty + (qz * tx - qx * tz);
    dst->vec3[2] = vz + qw * tz + (qx * ty - qy * tx);
}
