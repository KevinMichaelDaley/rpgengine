/**
 * @file aegis_ops_compare2.c
 * @brief Comparison ops: gt, ge. Bitwise ops: xor, not.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_gt(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->i32 > b->i32) ? 1u : 0u;
}

void aegis_op_ge(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->i32 >= b->i32) ? 1u : 0u;
}

void aegis_op_xor(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = a->u32 ^ b->u32;
}

void aegis_op_not(aegis_register_t *dst, const aegis_register_t *a) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = ~a->u32;
}
