/**
 * @file aegis_ops_compare.c
 * @brief Comparison ops: eq, ne, lt, le. Bitwise ops: xor, not.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 * Comparison results are bool (u32: 0 or 1).
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_eq(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->i32 == b->i32) ? 1u : 0u;
}

void aegis_op_ne(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->i32 != b->i32) ? 1u : 0u;
}

void aegis_op_lt(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->i32 < b->i32) ? 1u : 0u;
}

void aegis_op_le(aegis_register_t *dst,
                 const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->i32 <= b->i32) ? 1u : 0u;
}
