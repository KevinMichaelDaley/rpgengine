#ifndef FERRUM_PHYSICS_MANIFOLD_CACHE_H
#define FERRUM_PHYSICS_MANIFOLD_CACHE_H

/** @file
 * @brief Persistent manifold cache for warmstarting the contact solver.
 *
 * Stores contact manifolds keyed by body pair, enabling accumulated
 * impulses to survive across simulation frames. Uses open-addressing
 * hash table with linear probing for O(1) average lookup.
 */

#include "ferrum/physics/manifold.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel value indicating an empty hash table slot. */
#define PHYS_CACHE_INVALID_INDEX 0xFFFFFFFF

/**
 * @brief A single cached manifold with metadata.
 *
 * Pairs a contact manifold with the body-pair key and timing
 * information used for expiry.
 */
typedef struct phys_manifold_cache_entry {
    uint64_t pair_key;           /**< body_a << 32 | body_b (smaller ID first). */
    phys_manifold_t manifold;    /**< The cached contact manifold. */
    uint32_t last_used_tick;     /**< Tick when this entry was last active. */
    uint32_t flags;              /**< Reserved flags. */
} phys_manifold_cache_entry_t;

/**
 * @brief Hash table of persistent manifolds for warmstarting.
 *
 * Owns the entries array and the hash table. Caller must call
 * phys_manifold_cache_init() before use and phys_manifold_cache_destroy()
 * when done.
 */
typedef struct phys_manifold_cache {
    phys_manifold_cache_entry_t *entries; /**< Dense array of cache entries. */
    uint32_t capacity;           /**< Maximum number of entries. */
    uint32_t count;              /**< Current number of active entries. */
    uint32_t *hash_table;        /**< Maps hash slots to entry indices, or PHYS_CACHE_INVALID_INDEX. */
    uint32_t hash_size;          /**< Number of hash table slots (power of 2). */
    uint32_t hash_mask;          /**< hash_size - 1, for fast modulo. */
} phys_manifold_cache_t;

/**
 * @brief Initialize a manifold cache with the given capacity.
 *
 * Allocates the entries array and hash table. The hash table size is
 * the next power of two >= capacity * 2 for a load factor of ~0.5.
 *
 * @param cache  Cache to initialize. If NULL, returns -1.
 * @param capacity  Maximum number of manifold entries.
 * @return 0 on success, -1 on failure (NULL cache or allocation failure).
 *
 * @note Ownership: caller owns the cache and must call
 *       phys_manifold_cache_destroy() to release memory.
 */
int phys_manifold_cache_init(phys_manifold_cache_t *cache, uint32_t capacity);

/**
 * @brief Destroy a manifold cache and free all memory.
 *
 * After this call the cache is zeroed and must not be used unless
 * re-initialized.
 *
 * @param cache  Cache to destroy. If NULL, no-op.
 */
void phys_manifold_cache_destroy(phys_manifold_cache_t *cache);

/**
 * @brief Find an existing manifold for a body pair.
 *
 * Body order does not matter — the pair key is normalized internally.
 *
 * @param cache  Cache to search. If NULL, returns NULL.
 * @param body_a  First body index.
 * @param body_b  Second body index.
 * @return Pointer to the cached manifold, or NULL if not found.
 *
 * @note The returned pointer is owned by the cache and remains valid
 *       until the entry is expired or the cache is destroyed.
 */
phys_manifold_t *phys_manifold_cache_find(phys_manifold_cache_t *cache,
                                          uint32_t body_a, uint32_t body_b);

/**
 * @brief Get an existing manifold or create a new one for a body pair.
 *
 * If the pair already exists, updates its last_used_tick and returns
 * the existing manifold. Otherwise allocates a new entry (if capacity
 * allows), initializes the manifold, and inserts it into the hash table.
 *
 * @param cache  Cache to search/insert. If NULL, returns NULL.
 * @param body_a  First body index.
 * @param body_b  Second body index.
 * @param tick    Current simulation tick for expiry tracking.
 * @return Pointer to the manifold, or NULL if the cache is full.
 *
 * @note The returned pointer is owned by the cache.
 */
phys_manifold_t *phys_manifold_cache_get_or_create(phys_manifold_cache_t *cache,
                                                   uint32_t body_a,
                                                   uint32_t body_b,
                                                   uint32_t tick);

/**
 * @brief Remove entries not used within the last max_age ticks.
 *
 * Any entry whose (current_tick - last_used_tick) > max_age is removed.
 * Removed entries are compacted by swapping with the last entry, and
 * the swapped entry is re-hashed.
 *
 * @param cache        Cache to expire from. If NULL, no-op.
 * @param current_tick Current simulation tick.
 * @param max_age      Maximum number of ticks an entry may be idle.
 */
void phys_manifold_cache_expire(phys_manifold_cache_t *cache,
                                uint32_t current_tick, uint32_t max_age);

/**
 * @brief Update the last_used_tick for a body pair.
 *
 * Has no effect if the pair is not in the cache.
 *
 * @param cache  Cache to update. If NULL, no-op.
 * @param body_a First body index.
 * @param body_b Second body index.
 * @param tick   Tick to set as the last-used time.
 */
void phys_manifold_cache_touch(phys_manifold_cache_t *cache,
                               uint32_t body_a, uint32_t body_b,
                               uint32_t tick);

/**
 * @brief Return the number of active entries in the cache.
 *
 * @param cache  Cache to query. If NULL, returns 0.
 * @return Number of active entries.
 */
uint32_t phys_manifold_cache_count(const phys_manifold_cache_t *cache);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_MANIFOLD_CACHE_H */
