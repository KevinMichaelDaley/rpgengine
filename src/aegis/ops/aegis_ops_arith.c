/**
 * @file aegis_ops_arith.c
 * @brief Arithmetic ops: add, sub, mul, div.
 *
 * Per ref/aegis_bytecode_spec.md §3.3. Operates on i32 fields.
 * Uses unsigned intermediate to avoid signed overflow UB.
 */

#include "ferrum/aegis/aegis_ops_arith.h"
#include <string.h>

bool aegis_op_add(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    uint32_t result = (uint32_t)a->i32 + (uint32_t)b->i32;
    memset(dst, 0, sizeof(*dst));
    memcpy(&dst->i32, &result, sizeof(int32_t));
    return true;
}

bool aegis_op_sub(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    uint32_t result = (uint32_t)a->i32 - (uint32_t)b->i32;
    memset(dst, 0, sizeof(*dst));
    memcpy(&dst->i32, &result, sizeof(int32_t));
    return true;
}

bool aegis_op_mul(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    uint32_t result = (uint32_t)a->i32 * (uint32_t)b->i32;
    memset(dst, 0, sizeof(*dst));
    memcpy(&dst->i32, &result, sizeof(int32_t));
    return true;
}

bool aegis_op_div(aegis_register_t *dst,
                  const aegis_register_t *a, const aegis_register_t *b) {
    if (b->i32 == 0) {
        return false;
    }
    memset(dst, 0, sizeof(*dst));
    dst->i32 = a->i32 / b->i32;
    return true;
}
