/**
 * @file aegis_ops_arith2.c
 * @brief Arithmetic ops: mod, neg. Bitwise ops: and, or.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

bool aegis_op_mod(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    if (b->i32 == 0) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->i32 = a->i32 % b->i32;
    return true;
}

bool aegis_op_neg(aegis_register_t *dst, const aegis_register_t *a) {
    uint32_t result = -(uint32_t)a->i32;
    memset(dst, 0, sizeof(*dst));
    memcpy(&dst->i32, &result, sizeof(int32_t));
    return true;
}

void aegis_op_and(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = a->u32 & b->u32;
}

void aegis_op_or(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = a->u32 | b->u32;
}
