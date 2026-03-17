/**
 * @file snap_mesh_cache_query.c
 * @brief Snap mesh cache read-only queries.
 *
 * Non-static functions (2 / 4 limit):
 *   snap_mesh_cache_get
 *   snap_mesh_cache_has
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"

#include <stddef.h>

const snap_mesh_t *snap_mesh_cache_get(const snap_mesh_cache_t *cache,
                                        uint32_t entity_id) {
    if (!cache || !cache->meshes) return NULL;
    if (entity_id >= cache->capacity) return NULL;
    const snap_mesh_t *slot = &cache->meshes[entity_id];
    if (!slot->positions) return NULL;
    return slot;
}

bool snap_mesh_cache_has(const snap_mesh_cache_t *cache, uint32_t entity_id) {
    if (!cache || !cache->meshes) return false;
    if (entity_id >= cache->capacity) return false;
    return cache->meshes[entity_id].positions != NULL;
}
