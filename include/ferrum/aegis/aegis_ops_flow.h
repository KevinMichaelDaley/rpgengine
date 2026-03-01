/**
 * @file aegis_ops_flow.h
 * @brief Aegis control flow instruction handlers.
 *
 * Per ref/aegis_bytecode_spec.md §3.3.
 *
 * Jump instructions modify the program counter.
 * call/ret interact with the memory module's call stack.
 *
 * Ownership: callers own all pointers.
 * Nullability: all pointers must be non-NULL.
 */

#ifndef FERRUM_AEGIS_OPS_FLOW_H
#define FERRUM_AEGIS_OPS_FLOW_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_memory.h"
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Unconditional jump.
 *
 * @param label         Target instruction index.
 * @param bytecode_len  Total number of instructions in bytecode.
 * @param out_pc        Output: new program counter.
 * @return true if label is within bounds, false otherwise.
 */
bool aegis_op_jmp(uint32_t label, uint32_t bytecode_len, uint32_t *out_pc);

/**
 * @brief Conditional jump (if truthy).
 *
 * Jumps if cond->u32 is nonzero.
 *
 * @param cond          Condition register.
 * @param label         Target instruction index.
 * @param current_pc    Current PC (for fall-through case).
 * @param bytecode_len  Total number of instructions.
 * @param out_pc        Output: new program counter.
 * @return true on success, false if label out of bounds.
 */
bool aegis_op_jmp_if(const aegis_register_t *cond, uint32_t label,
                     uint32_t current_pc, uint32_t bytecode_len,
                     uint32_t *out_pc);

/**
 * @brief Conditional jump (if falsy).
 *
 * Jumps if cond->u32 is zero.
 */
bool aegis_op_jmp_if_not(const aegis_register_t *cond, uint32_t label,
                         uint32_t current_pc, uint32_t bytecode_len,
                         uint32_t *out_pc);

/**
 * @brief Function call.
 *
 * Pushes current_pc + 1 as return address onto call stack, jumps to label.
 *
 * @param mem           Memory manager (for call stack).
 * @param current_pc    Current program counter.
 * @param label         Target instruction index.
 * @param bytecode_len  Total number of instructions.
 * @param out_pc        Output: new program counter.
 * @return true on success, false on stack overflow or label out of bounds.
 */
bool aegis_op_call(aegis_memory_t *mem, uint32_t current_pc,
                   uint32_t label, uint32_t bytecode_len,
                   uint32_t *out_pc);

/**
 * @brief Function return.
 *
 * Pops return address from call stack.
 *
 * @param mem    Memory manager.
 * @param out_pc Output: restored program counter.
 * @return true on success, false on stack underflow.
 */
bool aegis_op_ret(aegis_memory_t *mem, uint32_t *out_pc);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_OPS_FLOW_H */
