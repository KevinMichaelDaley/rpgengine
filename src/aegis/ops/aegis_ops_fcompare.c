/**
 * @file aegis_ops_fcompare.c
 * @brief Float comparison ops: flt, fle, fgt, fge.
 *
 * Comparison results are bool (u32: 0 or 1).
 * These compare the f32 field of registers, unlike lt/le/gt/ge
 * which compare i32.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_flt(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->f32 < b->f32) ? 1u : 0u;
}

void aegis_op_fle(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->f32 <= b->f32) ? 1u : 0u;
}

void aegis_op_fgt(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->f32 > b->f32) ? 1u : 0u;
}

void aegis_op_fge(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = (a->f32 >= b->f32) ? 1u : 0u;
}
