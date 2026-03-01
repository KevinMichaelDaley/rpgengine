/**
 * @file aegis_ops_data.c
 * @brief Data movement ops: mov, load_imm, load_imm64.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 */

#include "ferrum/aegis/aegis_ops_data.h"
#include <string.h>

void aegis_op_mov(aegis_register_t *dst, const aegis_register_t *src) {
    *dst = *src;
}

void aegis_op_load_imm(aegis_register_t *dst, uint32_t imm) {
    memset(dst, 0, sizeof(*dst));
    dst->u32 = imm;
}

void aegis_op_load_imm64(aegis_register_t *dst, uint32_t lo, uint32_t hi) {
    memset(dst, 0, sizeof(*dst));
    dst->u64 = ((uint64_t)hi << 32) | (uint64_t)lo;
}
