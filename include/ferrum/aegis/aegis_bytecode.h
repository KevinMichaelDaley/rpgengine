/**
 * @file aegis_bytecode.h
 * @brief Aegis compiled bytecode container.
 *
 * Per ref/aegis_bytecode_spec.md §3.4.
 *
 * A bytecode module holds the instruction array, constant pool,
 * static size declaration, and subscribed event topic hash.
 *
 * Ownership: the bytecode module owns its instruction and constant arrays.
 * The caller is responsible for freeing them via aegis_bytecode_destroy().
 */

#ifndef FERRUM_AEGIS_BYTECODE_H
#define FERRUM_AEGIS_BYTECODE_H

#include <stdint.h>
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compiled bytecode module.
 *
 * Contains everything needed to instantiate a script VM:
 * the instruction stream, compile-time constants, the static
 * memory requirement, and the event topic this script subscribes to.
 */
typedef struct aegis_bytecode {
    /** Instruction array (owned, heap-allocated). NULL if empty. */
    aegis_instruction_t *instructions;

    /** Number of instructions. */
    uint32_t instruction_count;

    /** Constant pool (owned, heap-allocated). NULL if empty.
     *  Each constant is a 128-bit register value. */
    aegis_register_t *constants;

    /** Number of constants in the pool. */
    uint32_t constant_count;

    /** Static array size in bytes (declared by .static directive). */
    uint32_t static_size;

    /** Event topic hash this script subscribes to (from !topic name). */
    uint32_t topic_hash;
} aegis_bytecode_t;

/**
 * @brief Initialize a bytecode module to empty state.
 *
 * @param bc Pointer to bytecode structure. Must not be NULL.
 *
 * Side effects: zeroes all fields.
 * Error semantics: none (always succeeds).
 */
static inline void aegis_bytecode_init(aegis_bytecode_t *bc) {
    bc->instructions     = (aegis_instruction_t *)0;
    bc->instruction_count = 0;
    bc->constants        = (aegis_register_t *)0;
    bc->constant_count   = 0;
    bc->static_size      = 0;
    bc->topic_hash       = 0;
}

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_BYTECODE_H */
