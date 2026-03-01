/**
 * @file aegis_ops_vec3b.c
 * @brief Vec3 ops: dot, cross, len, norm.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_math.h"
#include <math.h>
#include <string.h>

void aegis_op_vec3_dot(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = a->vec3[0] * b->vec3[0]
             + a->vec3[1] * b->vec3[1]
             + a->vec3[2] * b->vec3[2];
}

void aegis_op_vec3_cross(aegis_register_t *dst,
                         const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->vec3[0] = a->vec3[1] * b->vec3[2] - a->vec3[2] * b->vec3[1];
    dst->vec3[1] = a->vec3[2] * b->vec3[0] - a->vec3[0] * b->vec3[2];
    dst->vec3[2] = a->vec3[0] * b->vec3[1] - a->vec3[1] * b->vec3[0];
}

void aegis_op_vec3_len(aegis_register_t *dst, const aegis_register_t *a) {
    memset(dst, 0, sizeof(*dst));
    float sq = a->vec3[0] * a->vec3[0]
             + a->vec3[1] * a->vec3[1]
             + a->vec3[2] * a->vec3[2];
    dst->f32 = sqrtf(sq);
}

void aegis_op_vec3_norm(aegis_register_t *dst, const aegis_register_t *a) {
    memset(dst, 0, sizeof(*dst));
    float sq = a->vec3[0] * a->vec3[0]
             + a->vec3[1] * a->vec3[1]
             + a->vec3[2] * a->vec3[2];
    if (sq < 1e-12f) {
        /* Zero vector → zero result (no NaN). */
        return;
    }
    float inv_len = 1.0f / sqrtf(sq);
    dst->vec3[0] = a->vec3[0] * inv_len;
    dst->vec3[1] = a->vec3[1] * inv_len;
    dst->vec3[2] = a->vec3[2] * inv_len;
}
