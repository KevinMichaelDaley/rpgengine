/**
 * @file aegis_ops_convert2.c
 * @brief Type conversion ops: f64↔f32.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_f64_to_f32(aegis_register_t *dst, const aegis_register_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = (float)src->f64;
}

void aegis_op_f32_to_f64(aegis_register_t *dst, const aegis_register_t *src) {
    memset(dst, 0, sizeof(*dst));
    dst->f64 = (double)src->f32;
}
