/**
 * @file aegis_ops_data.h
 * @brief Aegis data movement instruction handlers.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 *
 * Covers: mov, load_imm, load_imm64.
 * Memory ops (alloc, load, store, static_load/store, push, pop)
 * are handled directly via aegis_memory.h functions.
 *
 * Ownership: callers own all pointers.
 * Nullability: all pointers must be non-NULL.
 */

#ifndef FERRUM_AEGIS_OPS_DATA_H
#define FERRUM_AEGIS_OPS_DATA_H

#include <stdint.h>
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Copy register (full 128 bits).
 *
 * @param dst Destination register.
 * @param src Source register.
 */
void aegis_op_mov(aegis_register_t *dst, const aegis_register_t *src);

/**
 * @brief Load 32-bit immediate into register.
 *
 * Zero-extends to 128 bits (clears upper bits).
 *
 * @param dst Destination register.
 * @param imm 32-bit immediate value.
 */
void aegis_op_load_imm(aegis_register_t *dst, uint32_t imm);

/**
 * @brief Load 64-bit immediate into register.
 *
 * lo goes into the lower 32 bits, hi into the upper 32 bits
 * of the 64-bit field. Upper 64 bits of register are zeroed.
 *
 * @param dst Destination register.
 * @param lo  Lower 32 bits.
 * @param hi  Upper 32 bits.
 */
void aegis_op_load_imm64(aegis_register_t *dst, uint32_t lo, uint32_t hi);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_DATA_H */
