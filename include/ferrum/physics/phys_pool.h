#ifndef FERRUM_PHYSICS_PHYS_POOL_H
#define FERRUM_PHYSICS_PHYS_POOL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/memory/arena.h"
#include "ferrum/physics/body.h"

/** @file
 * @brief Double-buffered body pool and per-frame arena allocator.
 *
 * The body pool stores rigid bodies in two parallel arrays (curr/next)
 * to support lock-free double-buffered reads during physics updates.
 * Buffer swap is O(1) pointer exchange.
 *
 * The frame arena provides fast bump-pointer allocations for transient
 * per-tick data (contact manifolds, island lists, etc.).  Reset is O(1).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* ── Body pool ──────────────────────────────────────────────────── */

/**
 * @brief Double-buffered body pool with indexed access.
 *
 * Ownership: the pool owns all internal arrays.  Callers must not
 * free the body pointers returned by get_curr / get_next.
 *
 * Nullability: all public functions are NULL-safe on the pool pointer.
 */
typedef struct phys_body_pool {
    phys_body_t *bodies_curr;  /**< Read buffer (current frame). */
    phys_body_t *bodies_next;  /**< Write buffer (next frame). */
    uint32_t capacity;         /**< Maximum number of bodies. */
    uint32_t count;            /**< Number of active bodies. */
    uint8_t *active;           /**< Per-slot activity flag (1 = in use). */
} phys_body_pool_t;

/* ── Frame arena ────────────────────────────────────────────────── */

/**
 * @brief Bump-pointer arena for per-frame transient allocations.
 *
 * Wraps the general-purpose arena_t allocator and adds ownership of
 * the backing buffer (malloc'd on init, freed on destroy).  Pointers
 * returned by phys_frame_arena_alloc are invalidated after reset or
 * destroy.
 *
 * Nullability: all public functions are NULL-safe on the arena pointer.
 */
typedef struct phys_frame_arena {
    arena_t  arena;    /**< Underlying linear arena. */
    uint8_t *buffer;   /**< Owned backing buffer (malloc'd). */
} phys_frame_arena_t;

/* ── Body pool API ──────────────────────────────────────────────── */

/**
 * @brief Initialize a body pool with the given capacity.
 *
 * Allocates two body arrays and one activity array, all zeroed.
 *
 * @param pool      Pool to initialize (non-NULL).
 * @param capacity  Maximum number of bodies (must be > 0).
 * @return 0 on success, -1 on invalid arguments or allocation failure.
 *
 * Error semantics: returns -1 if pool is NULL or capacity is 0.
 * Side effects: allocates memory via calloc.
 */
int phys_body_pool_init(phys_body_pool_t *pool, uint32_t capacity);

/**
 * @brief Destroy the pool and free all internal arrays.
 *
 * @param pool  Pool to destroy (NULL-safe, no-op if NULL).
 *
 * Side effects: frees memory; zeroes the struct.
 */
void phys_body_pool_destroy(phys_body_pool_t *pool);

/**
 * @brief Add a body to the pool.
 *
 * Finds the first inactive slot, copies the body into both curr and
 * next buffers, and marks the slot active.
 *
 * @param pool      Body pool (non-NULL).
 * @param body      Body to copy in (non-NULL).
 * @param index_out Receives the assigned slot index (non-NULL).
 * @return 0 on success, -1 if pool is full or arguments are NULL.
 *
 * Ownership: the pool copies the body; the caller retains ownership
 * of the original.
 */
int phys_body_pool_add(phys_body_pool_t *pool, const phys_body_t *body, uint32_t *index_out);

/**
 * @brief Check whether a slot is active.
 *
 * @param pool   Body pool (NULL returns false).
 * @param index  Slot index.
 * @return true if the slot is in use; false otherwise.
 */
bool phys_body_pool_is_active(const phys_body_pool_t *pool, uint32_t index);

/**
 * @brief Get a pointer to the current-frame body at the given index.
 *
 * @param pool   Body pool (NULL returns NULL).
 * @param index  Slot index.
 * @return Pointer to the body in the read buffer, or NULL if inactive
 *         or out of range.
 *
 * Ownership: the returned pointer is owned by the pool.
 */
phys_body_t *phys_body_pool_get_curr(phys_body_pool_t *pool, uint32_t index);

/**
 * @brief Get a pointer to the next-frame body at the given index.
 *
 * @param pool   Body pool (NULL returns NULL).
 * @param index  Slot index.
 * @return Pointer to the body in the write buffer, or NULL if inactive
 *         or out of range.
 *
 * Ownership: the returned pointer is owned by the pool.
 */
phys_body_t *phys_body_pool_get_next(phys_body_pool_t *pool, uint32_t index);

/**
 * @brief Swap the curr and next body buffers (O(1) pointer exchange).
 *
 * After swapping, what was the write buffer becomes the read buffer.
 *
 * @param pool  Body pool (NULL-safe, no-op if NULL).
 */
void phys_body_pool_swap_buffers(phys_body_pool_t *pool);

/**
 * @brief Remove a body from the pool.
 *
 * Marks the slot inactive, zeroes both curr and next entries, and
 * decrements the active count.
 *
 * @param pool   Body pool (NULL-safe, no-op if NULL).
 * @param index  Slot index (out-of-range or already-inactive is a no-op).
 *
 * Side effects: zeroes the body data in both buffers.
 */
void phys_body_pool_remove(phys_body_pool_t *pool, uint32_t index);

/**
 * @brief Return the number of active bodies.
 *
 * @param pool  Body pool (NULL returns 0).
 * @return Active body count.
 */
uint32_t phys_body_pool_active_count(const phys_body_pool_t *pool);

/* ── Frame arena API ────────────────────────────────────────────── */

/**
 * @brief Initialize a frame arena with the given byte capacity.
 *
 * @param arena  Arena to initialize (non-NULL).
 * @param size   Backing buffer size in bytes (must be > 0).
 * @return 0 on success, -1 on invalid arguments or allocation failure.
 *
 * Side effects: allocates memory via malloc.
 */
int phys_frame_arena_init(phys_frame_arena_t *arena, size_t size);

/**
 * @brief Destroy the arena and free its backing buffer.
 *
 * @param arena  Arena to destroy (NULL-safe, no-op if NULL).
 *
 * Side effects: frees memory; zeroes the struct.
 */
void phys_frame_arena_destroy(phys_frame_arena_t *arena);

/**
 * @brief Allocate a block from the arena with the specified alignment.
 *
 * @param arena  Arena (NULL returns NULL).
 * @param size   Allocation size in bytes.
 * @param align  Required alignment (must be a power of two).
 * @return Pointer to the allocated block, or NULL if the arena cannot
 *         satisfy the request.
 *
 * Ownership: the returned pointer is owned by the arena and is
 * invalidated by phys_frame_arena_reset or phys_frame_arena_destroy.
 */
void *phys_frame_arena_alloc(phys_frame_arena_t *arena, size_t size, size_t align);

/**
 * @brief Reset the arena, freeing all allocations (O(1)).
 *
 * @param arena  Arena (NULL-safe, no-op if NULL).
 *
 * Side effects: invalidates all previously returned pointers.
 */
void phys_frame_arena_reset(phys_frame_arena_t *arena);

/**
 * @brief Return the number of bytes currently in use.
 *
 * @param arena  Arena (NULL returns 0).
 * @return Bytes used (includes alignment padding).
 */
size_t phys_frame_arena_used(const phys_frame_arena_t *arena);

/**
 * @brief Return the number of bytes remaining in the arena.
 *
 * @param arena  Arena (NULL returns 0).
 * @return Bytes remaining.
 */
size_t phys_frame_arena_remaining(const phys_frame_arena_t *arena);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_POOL_H */
