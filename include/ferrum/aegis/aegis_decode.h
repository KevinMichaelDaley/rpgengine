/**
 * @file aegis_decode.h
 * @brief Aegis instruction decoder.
 *
 * Per ref/aegis_bytecode_spec.md §3.4.
 *
 * Decodes a 128-bit instruction into opcode + resolved operand values.
 * Register-mode operands are read from the register file.
 * Immediate-mode operands are passed through as raw uint32 values.
 *
 * Ownership: the register file is read-only during decode.
 * Nullability: all pointer parameters must be non-NULL.
 */

#ifndef FERRUM_AEGIS_DECODE_H
#define FERRUM_AEGIS_DECODE_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Result of decoding a single instruction.
 */
typedef struct aegis_decode_result {
    aegis_opcode_t   opcode;  /**< Decoded opcode. */
    aegis_register_t a;       /**< Resolved operand A (register value or immediate). */
    aegis_register_t b;       /**< Resolved operand B (register value or immediate). */
    aegis_register_t c;       /**< Resolved operand C (register value or immediate). */
    uint32_t         raw_a;   /**< Raw operand A from instruction word 1. */
    uint32_t         raw_b;   /**< Raw operand B from instruction word 2. */
    uint32_t         raw_c;   /**< Raw operand C from instruction word 3. */
    bool             imm_a;   /**< True if operand A is an immediate. */
    bool             imm_b;   /**< True if operand B is an immediate. */
    bool             imm_c;   /**< True if operand C is an immediate. */
} aegis_decode_result_t;

/**
 * @brief Decode a single 128-bit instruction.
 *
 * Extracts the opcode and resolves each operand:
 * - If the corresponding immediate flag is set, the operand word
 *   is stored as-is (raw uint32 in the register's u32 field).
 * - If the flag is not set, the operand word is a register index
 *   (0-255) and the register's current value is copied.
 *
 * @param insn   Pointer to instruction. Must not be NULL.
 * @param regs   Register file (256 entries). Must not be NULL.
 * @param result Output decoded result. Must not be NULL.
 * @return true on success, false if a register index > 255.
 *
 * Side effects: none (pure read).
 * Error semantics: returns false on invalid register index;
 *   result contents are undefined on error.
 */
bool aegis_decode(const aegis_instruction_t *insn,
                  const aegis_register_t regs[AEGIS_REGISTER_COUNT],
                  aegis_decode_result_t *result);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_DECODE_H */
