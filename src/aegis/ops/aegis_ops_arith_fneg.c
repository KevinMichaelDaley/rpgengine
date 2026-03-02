/**
 * @file aegis_ops_arith_fneg.c
 * @brief Float unary negation: fneg.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

void aegis_op_fneg(aegis_register_t *dst, const aegis_register_t *a) {
    memset(dst, 0, sizeof(*dst));
    dst->f32 = -a->f32;
}
