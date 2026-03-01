/**
 * @file aegis_ops_vec3.c
 * @brief Vec3 ops: add, sub, mul, scale.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_math.h"
#include <string.h>

void aegis_op_vec3_add(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->vec3[0] = a->vec3[0] + b->vec3[0];
    dst->vec3[1] = a->vec3[1] + b->vec3[1];
    dst->vec3[2] = a->vec3[2] + b->vec3[2];
}

void aegis_op_vec3_sub(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->vec3[0] = a->vec3[0] - b->vec3[0];
    dst->vec3[1] = a->vec3[1] - b->vec3[1];
    dst->vec3[2] = a->vec3[2] - b->vec3[2];
}

void aegis_op_vec3_mul(aegis_register_t *dst,
                       const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->vec3[0] = a->vec3[0] * b->vec3[0];
    dst->vec3[1] = a->vec3[1] * b->vec3[1];
    dst->vec3[2] = a->vec3[2] * b->vec3[2];
}

void aegis_op_vec3_scale(aegis_register_t *dst,
                         const aegis_register_t *vec,
                         const aegis_register_t *scalar) {
    memset(dst, 0, sizeof(*dst));
    float s = scalar->f32;
    dst->vec3[0] = vec->vec3[0] * s;
    dst->vec3[1] = vec->vec3[1] * s;
    dst->vec3[2] = vec->vec3[2] * s;
}
