/**
 * @file snap_decompose_cache_query.c
 * @brief Read-only queries for the decompose result cache.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_decompose_cache_get
 *   snap_decompose_cache_has
 */

#include "ferrum/editor/viewport/snap/snap_decompose_cache.h"
#include "ferrum/physics/convex_decompose.h"

#include <stddef.h>

const phys_decompose_result_t *snap_decompose_cache_get(
    const snap_decompose_cache_t *cache, uint32_t entity_id) {
    if (!cache || !cache->entries) return NULL;
    if (entity_id >= cache->capacity) return NULL;
    const snap_decompose_entry_t *entry = &cache->entries[entity_id];
    if (!entry->active || !entry->result) return NULL;
    return entry->result;
}

bool snap_decompose_cache_has(const snap_decompose_cache_t *cache,
                               uint32_t entity_id) {
    if (!cache || !cache->entries) return false;
    if (entity_id >= cache->capacity) return false;
    return cache->entries[entity_id].active;
}
