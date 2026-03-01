/**
 * @file aegis_ops_convert.c
 * @brief Type conversion ops.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_i32_to_f32(aegis_register_t *dst, const aegis_register_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = (float)src->i32;
}

void aegis_op_f32_to_i32(aegis_register_t *dst, const aegis_register_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->i32 = (int32_t)src->f32;
}

void aegis_op_i64_to_f64(aegis_register_t *dst, const aegis_register_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->f64 = (double)src->i64;
}

void aegis_op_f64_to_i64(aegis_register_t *dst, const aegis_register_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->i64 = (int64_t)src->f64;
}
