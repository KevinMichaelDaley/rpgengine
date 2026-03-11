#ifndef FERRUM_MEMORY_ARENA_H
#define FERRUM_MEMORY_ARENA_H

#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Linear arena allocator (thread-safe via atomic bump pointer).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Arena allocator backed by caller-owned buffer.
 *  Thread-safe: the offset is atomic, allowing concurrent alloc calls. */
typedef struct arena {
    uint8_t *buffer;
    size_t capacity;
    atomic_size_t offset;
} arena_t;

/**
 * @brief Initialize arena with a backing buffer.
 * @param arena Arena pointer (non-NULL).
 * @param buffer Backing memory buffer.
 * @param capacity Buffer size in bytes.
 */
void arena_init(arena_t *arena, void *buffer, size_t capacity);

/**
 * @brief Allocate aligned memory from the arena.
 * @param arena Arena pointer.
 * @param alignment Alignment in bytes (power of two).
 * @param size Allocation size in bytes.
 * @return Pointer to allocation or NULL on failure.
 */
void *arena_alloc(arena_t *arena, size_t alignment, size_t size);

/**
 * @brief Reset arena to empty (invalidates prior allocations).
 * @param arena Arena pointer.
 */
void arena_reset(arena_t *arena);

/**
 * @brief Record current offset for nested lifetimes.
 * @param arena Arena pointer.
 * @return Current offset mark.
 */
size_t arena_mark(const arena_t *arena);

/**
 * @brief Restore arena offset to a previous mark.
 * @param arena Arena pointer.
 * @param mark Mark to restore.
 * @return 0 on success, -1 on invalid mark.
 */
int arena_pop_to_mark(arena_t *arena, size_t mark);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MEMORY_ARENA_H */
