/**
 * @file aegis_memory.h
 * @brief Aegis VM three-zone memory layout.
 *
 * Per ref/aegis_bytecode_spec.md §3.6.
 *
 * Layout within a single contiguous buffer:
 *   [Static Array | Call Stack | Heap Arena]
 *
 * - Static array: persistent mutable state, survives all yields.
 * - Call stack: return addresses + register saves, persists across
 *   force-yield and wait-yield.
 * - Heap arena: bump allocator, reset on explicit yield only.
 *
 * All accesses are bounds-checked. Overflow causes error return,
 * never undefined behavior.
 *
 * Ownership: the caller owns the backing buffer. aegis_memory_t
 * is a non-owning view into it.
 * Nullability: all pointer parameters must be non-NULL.
 */

#ifndef FERRUM_AEGIS_MEMORY_H
#define FERRUM_AEGIS_MEMORY_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/aegis/aegis_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-script memory manager (three-zone layout).
 *
 * Does not own the backing buffer. The caller must ensure the
 * buffer outlives this struct.
 */
typedef struct aegis_memory {
    uint8_t *base;           /**< Start of backing buffer. */
    uint32_t arena_size;     /**< Total buffer size. */

    /* Zone boundaries (byte offsets from base). */
    uint32_t static_end;     /**< End of static array = start of stack. */
    uint32_t stack_limit;    /**< End of stack = start of heap. */

    /* Stack state. */
    uint32_t stack_top;      /**< Current stack pointer (grows upward from static_end). */
    uint32_t call_depth;     /**< Number of active call frames. */

    /* Heap state. */
    uint32_t heap_bump;      /**< Next free byte in heap (grows upward from stack_limit). */
} aegis_memory_t;

/**
 * @brief Initialize memory layout.
 *
 * Partitions buf into [static | stack | heap]. Heap occupies
 * the remainder after static + stack.
 *
 * @param mem         Output memory manager.
 * @param buf         Backing buffer (caller-owned). Must not be NULL.
 * @param arena_size  Total size of buf in bytes. Must be > 0.
 * @param static_size Size of static array zone in bytes.
 * @param stack_size  Size of call stack zone in bytes.
 * @return true on success, false if arena_size is 0 or < static_size + stack_size.
 *
 * Side effects: zeroes the static array zone.
 * Error semantics: returns false on invalid parameters; mem is unmodified.
 */
bool aegis_memory_init(aegis_memory_t *mem, uint8_t *buf,
                       uint32_t arena_size, uint32_t static_size,
                       uint32_t stack_size);

/**
 * @brief Load 16 bytes from the static array.
 *
 * @param mem    Memory manager.
 * @param offset Byte offset within static zone (must be <= static_end - 16).
 * @param out    Output register.
 * @return true on success, false if out-of-bounds.
 */
bool aegis_memory_static_load(const aegis_memory_t *mem, uint32_t offset,
                              aegis_register_t *out);

/**
 * @brief Store 16 bytes to the static array.
 *
 * @param mem    Memory manager.
 * @param offset Byte offset within static zone (must be <= static_end - 16).
 * @param val    Value to store.
 * @return true on success, false if out-of-bounds.
 */
bool aegis_memory_static_store(aegis_memory_t *mem, uint32_t offset,
                               const aegis_register_t *val);

/**
 * @brief Push a 16-byte register value onto the call stack.
 *
 * @param mem Memory manager.
 * @param val Value to push.
 * @return true on success, false on stack overflow.
 */
bool aegis_memory_stack_push(aegis_memory_t *mem,
                             const aegis_register_t *val);

/**
 * @brief Pop a 16-byte register value from the call stack.
 *
 * @param mem Memory manager.
 * @param out Output register.
 * @return true on success, false on stack underflow.
 */
bool aegis_memory_stack_pop(aegis_memory_t *mem, aegis_register_t *out);

/**
 * @brief Push a call frame (return PC stored on stack).
 *
 * @param mem       Memory manager.
 * @param return_pc Program counter to return to on ret.
 * @return true on success, false on stack overflow.
 */
bool aegis_memory_push_frame(aegis_memory_t *mem, uint32_t return_pc);

/**
 * @brief Pop a call frame.
 *
 * @param mem          Memory manager.
 * @param out_return_pc Output: the return PC.
 * @return true on success, false on underflow (no active frame).
 */
bool aegis_memory_pop_frame(aegis_memory_t *mem, uint32_t *out_return_pc);

/**
 * @brief Get current call depth (number of active frames).
 */
uint32_t aegis_memory_call_depth(const aegis_memory_t *mem);

/**
 * @brief Bump-allocate from the heap arena.
 *
 * @param mem  Memory manager.
 * @param size Number of bytes to allocate.
 * @return Byte offset from arena base, or -1 if heap is full.
 *
 * The returned offset is absolute within the backing buffer.
 */
int32_t aegis_memory_alloc(aegis_memory_t *mem, uint32_t size);

/**
 * @brief Reset the heap arena bump pointer.
 *
 * Called on explicit yield only. Does not affect static or stack zones.
 */
void aegis_memory_heap_reset(aegis_memory_t *mem);

/**
 * @brief Load 16 bytes from the heap arena.
 *
 * @param mem    Memory manager.
 * @param base   Base offset (absolute in backing buffer).
 * @param offset Additional byte offset from base.
 * @param out    Output register.
 * @return true on success, false if out-of-bounds or in wrong zone.
 */
bool aegis_memory_heap_load(const aegis_memory_t *mem,
                            uint32_t base, uint32_t offset,
                            aegis_register_t *out);

/**
 * @brief Store 16 bytes to the heap arena.
 *
 * @param mem    Memory manager.
 * @param base   Base offset (absolute in backing buffer).
 * @param offset Additional byte offset from base.
 * @param val    Value to store.
 * @return true on success, false if out-of-bounds or in wrong zone.
 */
bool aegis_memory_heap_store(aegis_memory_t *mem,
                             uint32_t base, uint32_t offset,
                             const aegis_register_t *val);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_MEMORY_H */
