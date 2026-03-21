/**
 * @file snap_decompose_cache.h
 * @brief Cache for per-entity convex decomposition results.
 *
 * Stores phys_decompose_result_t per entity ID so that convex
 * decomposition results can be reused for physics body creation
 * without re-running V-ACD. Results are heap-allocated since
 * phys_decompose_result_t is ~167KB (64 inline convex hulls).
 *
 * Public types (2 / 2 limit):
 *   snap_decompose_entry_t
 *   snap_decompose_cache_t
 */
#ifndef FERRUM_EDITOR_VIEWPORT_SNAP_DECOMPOSE_CACHE_H
#define FERRUM_EDITOR_VIEWPORT_SNAP_DECOMPOSE_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration. */
struct phys_decompose_result;

/**
 * @brief Single cache entry for a decomposition result.
 *
 * Result is heap-allocated and owned by the cache.
 */
typedef struct snap_decompose_entry {
    struct phys_decompose_result *result; /**< Heap-allocated result, or NULL. */
    bool active;                          /**< True if entry holds valid data. */
} snap_decompose_entry_t;

/**
 * @brief Cache mapping entity_id → phys_decompose_result_t.
 *
 * Parallel array indexed by entity_id. Capacity matches entity store.
 */
typedef struct snap_decompose_cache {
    snap_decompose_entry_t *entries; /**< Array of entries [capacity]. */
    uint32_t capacity;               /**< Maximum entity_id + 1. */
} snap_decompose_cache_t;

/* ---- Lifecycle (snap_decompose_cache.c) ---- */

/**
 * @brief Initialize decompose cache with given capacity.
 * @param cache    Cache to initialize (non-NULL).
 * @param capacity Maximum number of entity slots.
 */
void snap_decompose_cache_init(snap_decompose_cache_t *cache, uint32_t capacity);

/**
 * @brief Destroy cache, freeing all stored results.
 * @param cache  Cache to destroy (non-NULL).
 */
void snap_decompose_cache_destroy(snap_decompose_cache_t *cache);

/**
 * @brief Store a decomposition result for an entity (deep copy).
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID (must be < capacity).
 * @param result     Result to copy (non-NULL).
 */
void snap_decompose_cache_set(snap_decompose_cache_t *cache,
                               uint32_t entity_id,
                               const struct phys_decompose_result *result);

/**
 * @brief Remove a cached result for an entity.
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID.
 */
void snap_decompose_cache_remove(snap_decompose_cache_t *cache,
                                  uint32_t entity_id);

/* ---- Query (snap_decompose_cache_query.c) ---- */

/**
 * @brief Get a cached decomposition result.
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID.
 * @return Pointer to result, or NULL if not cached.
 */
const struct phys_decompose_result *snap_decompose_cache_get(
    const snap_decompose_cache_t *cache, uint32_t entity_id);

/**
 * @brief Check whether a decomposition result is cached.
 * @param cache      Cache (non-NULL).
 * @param entity_id  Entity ID.
 * @return true if cached.
 */
bool snap_decompose_cache_has(const snap_decompose_cache_t *cache,
                               uint32_t entity_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_SNAP_DECOMPOSE_CACHE_H */
