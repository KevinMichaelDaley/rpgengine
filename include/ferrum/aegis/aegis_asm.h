/**
 * @file aegis_asm.h
 * @brief Aegis text IL assembler: compiles asm-like source to bytecode.
 *
 * Per ref/aegis_bytecode_spec.md §3.3. The IL uses snake_case mnemonics
 * matching the opcode enum, register operands (r0..r255), immediates
 * (decimal, hex, float), labels, and directives (.topic, .static, .fuel).
 *
 * Types exposed:
 *   - aegis_asm_t — assembler state (label table, diagnostics)
 */

#ifndef FERRUM_AEGIS_ASM_H
#define FERRUM_AEGIS_ASM_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_bytecode.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum number of labels in a single program. */
#define AEGIS_ASM_MAX_LABELS 256

/** Maximum number of unresolved label references (forward refs). */
#define AEGIS_ASM_MAX_FIXUPS 512

/** Maximum error message length. */
#define AEGIS_ASM_MAX_ERROR  256

/**
 * @brief Label entry for the assembler's symbol table.
 */
typedef struct aegis_asm_label {
    char     name[64]; /**< Label name (without trailing colon). */
    uint32_t pc;       /**< Instruction index this label points to. */
    bool     defined;  /**< Whether the label has been defined. */
} aegis_asm_label_t;

/**
 * @brief Fixup entry for forward label references.
 */
typedef struct aegis_asm_fixup {
    char     label[64]; /**< Target label name. */
    uint32_t insn_idx;  /**< Instruction index needing fixup. */
    uint32_t operand;   /**< Which operand (0=A, 1=B, 2=C). */
    uint32_t line;      /**< Source line for error reporting. */
} aegis_asm_fixup_t;

/**
 * @brief Assembler state.
 *
 * Ownership: caller owns the assembler struct and output bytecode.
 * The assembler allocates instruction arrays via malloc.
 * Nullability: must call aegis_asm_init() before use.
 */
typedef struct aegis_asm {
    /** Label symbol table. */
    aegis_asm_label_t labels[AEGIS_ASM_MAX_LABELS];
    uint32_t          label_count;

    /** Forward reference fixups. */
    aegis_asm_fixup_t fixups[AEGIS_ASM_MAX_FIXUPS];
    uint32_t          fixup_count;

    /** Output instruction buffer (heap-allocated, grown as needed). */
    aegis_instruction_t *insns;
    uint32_t             insn_count;
    uint32_t             insn_cap;

    /** Extracted directives. */
    uint32_t static_size;  /**< From .static directive. */
    uint32_t topic_hash;   /**< From .topic directive. */
    uint32_t fuel_budget;  /**< From .fuel directive (0 = use default). */

    /** Error state. */
    bool     has_error;
    uint32_t error_line;
    char     error_msg[AEGIS_ASM_MAX_ERROR];
} aegis_asm_t;

/**
 * @brief Initialize an assembler instance.
 *
 * @param as Assembler to initialize. Must not be NULL.
 *
 * Side effects: zeroes all state.
 */
void aegis_asm_init(aegis_asm_t *as);

/**
 * @brief Compile IL source text to bytecode.
 *
 * Parses the source, resolves labels, and produces a bytecode module.
 * On success, out_bc is populated with heap-allocated instruction array
 * (caller must free via free()).
 *
 * @param as         Assembler state (reset on each call).
 * @param source     IL source text. Must not be NULL.
 * @param source_len Length of source in bytes.
 * @param out_bc     Output bytecode module. Must not be NULL.
 * @return true on success, false on error (call aegis_asm_error()).
 *
 * Ownership: on success, out_bc->instructions is heap-allocated.
 * Side effects: allocates memory for instruction array.
 */
bool aegis_asm_compile(aegis_asm_t *as, const char *source,
                       uint32_t source_len, aegis_bytecode_t *out_bc);

/**
 * @brief Return the error message from the last failed compile.
 *
 * @param as Assembler state.
 * @return Null-terminated error string, or "" if no error.
 */
const char *aegis_asm_error(const aegis_asm_t *as);

/**
 * @brief Return the line number where the error occurred.
 *
 * @param as Assembler state.
 * @return 1-based line number, or 0 if no error.
 */
uint32_t aegis_asm_error_line(const aegis_asm_t *as);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_ASM_H */
