#ifndef FERRUM_MEMORY_POOL_H
#define FERRUM_MEMORY_POOL_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Fixed-size pool allocator with generation handles.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Pool handle with index/generation. */
typedef struct pool_handle {
    uint32_t index;
    uint16_t generation;
    uint16_t flags;
} pool_handle_t;

/** Pool allocator for fixed-size elements. */
typedef struct pool {
    uint8_t *storage;
    uint32_t *free_list;
    uint16_t *generations;
    uint32_t capacity;
    uint32_t stride;
    uint32_t free_head;
} pool_t;

/** Pool allocation status codes. */
typedef enum pool_status {
    POOL_OK = 0,
    POOL_ERR_OOM = 1,
    POOL_ERR_INVALID = 2
} pool_status_t;

/** Invalid index sentinel. */
#define POOL_INDEX_INVALID UINT32_MAX

/**
 * @brief Initialize pool storage.
 * @param pool Pool pointer (non-NULL).
 * @param capacity Number of elements.
 * @param stride Size of each element in bytes.
 * @return POOL_OK on success, POOL_ERR_OOM on allocation failure.
 */
pool_status_t pool_init(pool_t *pool, uint32_t capacity, uint32_t stride);

/**
 * @brief Destroy pool storage.
 * @param pool Pool pointer.
 */
void pool_destroy(pool_t *pool);

/**
 * @brief Allocate a slot from the pool.
 * @param pool Pool pointer.
 * @return Handle with index/generation or invalid handle on failure.
 */
pool_handle_t pool_alloc(pool_t *pool);

/**
 * @brief Free a slot back to the pool.
 * @param pool Pool pointer.
 * @param handle Handle to free.
 * @return POOL_OK on success, POOL_ERR_INVALID on invalid handle.
 */
pool_status_t pool_free(pool_t *pool, pool_handle_t handle);

/**
 * @brief Get pointer for a handle if still valid.
 * @param pool Pool pointer.
 * @param handle Handle to query.
 * @return Pointer to element or NULL if invalid.
 */
void *pool_get(const pool_t *pool, pool_handle_t handle);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_MEMORY_POOL_H */
