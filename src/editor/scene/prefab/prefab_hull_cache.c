/**
 * @file prefab_hull_cache.c
 * @brief Generation-based cache for per-bone convex hulls.
 *
 * Non-static functions: prefab_hull_cache_init, prefab_hull_cache_invalidate,
 *                       prefab_hull_cache_rebuild, prefab_hull_cache_get (4/4).
 */

#include "ferrum/editor/scene/prefab/prefab_hull_cache.h"
#include "ferrum/editor/scene/prefab/prefab_hull_build.h"
#include "ferrum/editor/edit_entity.h"

#include <string.h>

void prefab_hull_cache_init(prefab_hull_cache_t *cache) {
    if (!cache) return;
    memset(cache, 0, sizeof(*cache));
}

void prefab_hull_cache_invalidate(prefab_hull_cache_t *cache,
                                  uint32_t bone_index) {
    if (!cache) return;

    for (uint32_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].bone_index == bone_index) {
            cache->entries[i].valid = false;
            return;
        }
    }
}

void prefab_hull_cache_rebuild(prefab_hull_cache_t *cache,
                               const struct edit_entity_store *entities,
                               uint32_t root_id,
                               uint32_t bone_count) {
    if (!cache || !entities) return;

    /* Cap bone_count to cache capacity. */
    if (bone_count > PREFAB_HULL_CACHE_MAX) {
        bone_count = PREFAB_HULL_CACHE_MAX;
    }

    cache->count = bone_count;
    cache->generation++;

    for (uint32_t b = 0; b < bone_count; b++) {
        prefab_hull_entry_t *entry = &cache->entries[b];
        entry->bone_index = b;

        /* Skip entries that are already valid and up-to-date. */
        if (entry->valid && entry->gen == cache->generation - 1) {
            entry->gen = cache->generation;
            continue;
        }

        prefab_hull_result_t result;
        bool ok = prefab_hull_build_from_markers(entities, root_id, b,
                                                  &result);
        if (ok) {
            entry->hull = result.hull;
            entry->valid = true;
        } else {
            entry->valid = false;
            memset(&entry->hull, 0, sizeof(entry->hull));
        }
        entry->gen = cache->generation;
    }
}

const prefab_hull_entry_t *prefab_hull_cache_get(
    const prefab_hull_cache_t *cache, uint32_t bone_index) {
    if (!cache) return NULL;

    for (uint32_t i = 0; i < cache->count; i++) {
        if (cache->entries[i].bone_index == bone_index) {
            return &cache->entries[i];
        }
    }
    return NULL;
}
