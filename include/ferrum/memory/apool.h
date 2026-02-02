#ifndef FERRUM_MEMORY_APOOL_H
#define FERRUM_MEMORY_APOOL_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/** @file
 * @brief Concurrent fixed-size pool allocator with generation handles.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Atomic pool handle with index/generation. */
typedef struct apool_handle {
    uint32_t index;       /**< Index in pool or APOOL_INDEX_INVALID */
    uint16_t generation;  /**< Generation counter for validity */
    uint16_t flags;       /**< Reserved */
} apool_handle_t;

/** Atomic pool allocator for fixed-size elements. */
typedef struct apool {
    uint8_t *storage;                       /**< Element storage */
    uint32_t *next;                         /**< Singly-linked stack next indices */
    _Atomic uint16_t *generations;          /**< Generation per slot */
    _Atomic uint32_t free_head;             /**< Lock-free stack head */
    uint32_t capacity;                      /**< Number of elements */
    uint32_t stride;                        /**< Size of each element */
} apool_t;

/** Pool allocation status codes. */
typedef enum apool_status {
    APOOL_OK = 0,
    APOOL_ERR_OOM = 1,
    APOOL_ERR_INVALID = 2
} apool_status_t;

/** Invalid index sentinel. */
#define APOOL_INDEX_INVALID UINT32_MAX

/**
 * @brief Initialize concurrent pool storage.
 * @param pool Pool pointer (non-NULL).
 * @param capacity Number of elements.
 * @param stride Size of each element in bytes.
 * @return APOOL_OK on success, APOOL_ERR_OOM on allocation failure.
 *
 * Ownership: Caller owns `pool` and must call `apool_destroy`.
 */
apool_status_t apool_init(apool_t *pool, uint32_t capacity, uint32_t stride);

/**
 * @brief Destroy pool storage.
 * @param pool Pool pointer.
 *
 * Side effects: releases allocated memory; safe to call with NULL.
 */
void apool_destroy(apool_t *pool);

/**
 * @brief Allocate a slot from the pool (lock-free).
 * @param pool Pool pointer.
 * @return Handle with index/generation or invalid handle on failure.
 *
 * Error semantics: returns `{APOOL_INDEX_INVALID,0,0}` when no free slots.
 * Thread-safety: concurrent allocations are safe.
 */
apool_handle_t apool_alloc(apool_t *pool);

/**
 * @brief Free a slot back to the pool (lock-free).
 * @param pool Pool pointer.
 * @param handle Handle to free.
 * @return APOOL_OK on success, APOOL_ERR_INVALID on invalid handle.
 *
 * Error semantics: invalid index or generation mismatch returns APOOL_ERR_INVALID.
 * Thread-safety: concurrent frees are safe.
 */
apool_status_t apool_free(apool_t *pool, apool_handle_t handle);

/**
 * @brief Get pointer for a handle if still valid.
 * @param pool Pool pointer.
 * @param handle Handle to query.
 * @return Pointer to element or NULL if invalid.
 *
 * Nullability: returns NULL if handle is stale or out-of-range.
 */
void *apool_get(const apool_t *pool, apool_handle_t handle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MEMORY_APOOL_H */
