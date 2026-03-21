/**
 * @file snap_decompose_cache.c
 * @brief Lifecycle and mutation for the decompose result cache.
 *
 * Non-static functions (4 / 4 limit):
 *   snap_decompose_cache_init
 *   snap_decompose_cache_destroy
 *   snap_decompose_cache_set
 *   snap_decompose_cache_remove
 */

#include "ferrum/editor/viewport/snap/snap_decompose_cache.h"
#include "ferrum/physics/convex_decompose.h"

#include <stdlib.h>
#include <string.h>

void snap_decompose_cache_init(snap_decompose_cache_t *cache,
                                uint32_t capacity) {
    if (!cache) return;
    cache->capacity = capacity;
    if (capacity == 0) {
        cache->entries = NULL;
        return;
    }
    cache->entries = calloc(capacity, sizeof(snap_decompose_entry_t));
}

void snap_decompose_cache_destroy(snap_decompose_cache_t *cache) {
    if (!cache || !cache->entries) return;
    for (uint32_t i = 0; i < cache->capacity; i++) {
        free(cache->entries[i].result);
    }
    free(cache->entries);
    cache->entries = NULL;
    cache->capacity = 0;
}

void snap_decompose_cache_set(snap_decompose_cache_t *cache,
                               uint32_t entity_id,
                               const phys_decompose_result_t *result) {
    if (!cache || !cache->entries || !result) return;
    if (entity_id >= cache->capacity) return;

    snap_decompose_entry_t *entry = &cache->entries[entity_id];

    /* Reuse existing allocation if possible. */
    if (!entry->result) {
        entry->result = malloc(sizeof(phys_decompose_result_t));
        if (!entry->result) return;
    }

    memcpy(entry->result, result, sizeof(phys_decompose_result_t));
    entry->active = true;
}

void snap_decompose_cache_remove(snap_decompose_cache_t *cache,
                                  uint32_t entity_id) {
    if (!cache || !cache->entries) return;
    if (entity_id >= cache->capacity) return;

    snap_decompose_entry_t *entry = &cache->entries[entity_id];
    free(entry->result);
    entry->result = NULL;
    entry->active = false;
}
