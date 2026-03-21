/**
 * @file prefab_hull_cache.h
 * @brief Generation-based cache for per-bone convex hulls.
 *
 * Stores pre-built convex hulls keyed by bone index. The generation
 * counter increments on each rebuild; individual bones can be
 * invalidated to trigger rebuild on the next pass.
 *
 * Ownership: value type, no heap allocations.
 * Nullability: all pointer params must be non-NULL.
 *
 * Public types: prefab_hull_entry_t, prefab_hull_cache_t (2-type rule).
 */
#ifndef FERRUM_EDITOR_SCENE_PREFAB_HULL_CACHE_H
#define FERRUM_EDITOR_SCENE_PREFAB_HULL_CACHE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/convex_hull.h"

/** Maximum bone entries in the hull cache. */
#define PREFAB_HULL_CACHE_MAX 256

/* Forward declaration. */
struct edit_entity_store;

/**
 * @brief A single cached hull entry for one bone.
 */
typedef struct prefab_hull_entry {
    uint32_t           bone_index;    /**< Bone index this entry is for. */
    phys_convex_hull_t hull;          /**< Cached hull (valid only if valid==true). */
    bool               valid;         /**< True if hull is up-to-date. */
    uint32_t           gen;           /**< Generation when last rebuilt. */
} prefab_hull_entry_t;

/**
 * @brief Cache of per-bone convex hulls with generation tracking.
 */
typedef struct prefab_hull_cache {
    prefab_hull_entry_t entries[PREFAB_HULL_CACHE_MAX];
    uint32_t count;       /**< Number of entries. */
    uint32_t generation;  /**< Global generation counter. */
} prefab_hull_cache_t;

/**
 * @brief Initialize the hull cache to empty.
 * @param cache  Cache to initialize (non-NULL).
 */
void prefab_hull_cache_init(prefab_hull_cache_t *cache);

/**
 * @brief Invalidate the hull for a specific bone.
 *
 * Marks the entry as invalid so it will be rebuilt on the next
 * rebuild pass.
 *
 * @param cache       Cache (non-NULL).
 * @param bone_index  Bone to invalidate.
 */
void prefab_hull_cache_invalidate(prefab_hull_cache_t *cache,
                                  uint32_t bone_index);

/**
 * @brief Rebuild all hull entries from the entity store.
 *
 * For each bone (0..bone_count-1), scans markers and rebuilds
 * the convex hull. Increments the generation counter.
 *
 * @param cache       Cache (non-NULL).
 * @param entities    Entity store (non-NULL).
 * @param root_id     Prefab root entity ID.
 * @param bone_count  Number of bones in the skeleton.
 */
void prefab_hull_cache_rebuild(prefab_hull_cache_t *cache,
                               const struct edit_entity_store *entities,
                               uint32_t root_id,
                               uint32_t bone_count);

/**
 * @brief Get the cached hull entry for a bone.
 * @param cache       Cache (non-NULL).
 * @param bone_index  Bone index.
 * @return Pointer to entry, or NULL if bone_index not in cache.
 */
const prefab_hull_entry_t *prefab_hull_cache_get(
    const prefab_hull_cache_t *cache, uint32_t bone_index);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_SCENE_PREFAB_HULL_CACHE_H */
