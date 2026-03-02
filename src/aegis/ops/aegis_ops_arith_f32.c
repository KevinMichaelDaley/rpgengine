/**
 * @file aegis_ops_arith_f32.c
 * @brief Float arithmetic ops: fadd, fsub, fmul, fdiv, fneg.
 *
 * Operates on f32 register fields.  fdiv returns false on division
 * by zero.  All others always succeed.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_fadd(aegis_register_t *dst,
                   const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = a->f32 + b->f32;
}

void aegis_op_fsub(aegis_register_t *dst,
                   const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = a->f32 - b->f32;
}

void aegis_op_fmul(aegis_register_t *dst,
                   const aegis_register_t *a, const aegis_register_t *b) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = a->f32 * b->f32;
}

bool aegis_op_fdiv(aegis_register_t *dst,
                   const aegis_register_t *a, const aegis_register_t *b) {
    if (b->f32 == 0.0f) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->f32 = a->f32 / b->f32;
    return true;
}
