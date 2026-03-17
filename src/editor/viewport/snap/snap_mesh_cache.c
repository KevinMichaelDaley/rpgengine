/**
 * @file snap_mesh_cache.c
 * @brief Snap mesh cache lifecycle and mutation.
 *
 * Non-static functions (4 / 4 limit):
 *   snap_mesh_cache_init
 *   snap_mesh_cache_destroy
 *   snap_mesh_cache_insert
 *   snap_mesh_cache_remove
 */

#include "ferrum/editor/viewport/snap/snap_mesh_cache.h"
#include "ferrum/memory/vm_alloc.h"

#include <stdlib.h>
#include <string.h>

/** @brief Free the heap-allocated buffers in a single snap mesh slot. */
static void free_slot_(snap_mesh_t *slot) {
    if (!slot) return;
    free(slot->positions);
    free(slot->normals);
    free(slot->indices);
    slot->positions = NULL;
    slot->normals = NULL;
    slot->indices = NULL;
    slot->vertex_count = 0;
    slot->index_count = 0;
}

void snap_mesh_cache_init(snap_mesh_cache_t *cache, uint32_t capacity) {
    if (!cache) return;
    if (capacity == 0) {
        cache->meshes = NULL;
        cache->capacity = 0;
        return;
    }
    /* Use demand-paged virtual memory for the slot array so large
     * capacities (millions of entities) don't require physical RAM
     * until slots are actually written. */
    cache->meshes = (snap_mesh_t *)vm_reserve(
        (size_t)capacity * sizeof(snap_mesh_t));
    cache->capacity = capacity;
}

void snap_mesh_cache_destroy(snap_mesh_cache_t *cache) {
    if (!cache) return;
    if (cache->meshes) {
        for (uint32_t i = 0; i < cache->capacity; i++) {
            free_slot_(&cache->meshes[i]);
        }
        vm_release(cache->meshes,
                   (size_t)cache->capacity * sizeof(snap_mesh_t));
    }
    cache->meshes = NULL;
    cache->capacity = 0;
}

void snap_mesh_cache_insert(snap_mesh_cache_t *cache, uint32_t entity_id,
                             const float *positions, const float *normals,
                             const uint32_t *indices,
                             uint32_t vertex_count, uint32_t index_count) {
    if (!cache || !cache->meshes) return;
    if (entity_id >= cache->capacity) return;
    if (!positions || !normals || !indices) return;
    if (vertex_count == 0 || index_count == 0) return;

    /* Free existing data if slot is occupied. */
    snap_mesh_t *slot = &cache->meshes[entity_id];
    free_slot_(slot);

    /* Allocate and copy. */
    size_t pos_bytes = (size_t)vertex_count * 3 * sizeof(float);
    size_t nrm_bytes = (size_t)vertex_count * 3 * sizeof(float);
    size_t idx_bytes = (size_t)index_count * sizeof(uint32_t);

    slot->positions = malloc(pos_bytes);
    slot->normals = malloc(nrm_bytes);
    slot->indices = malloc(idx_bytes);

    if (!slot->positions || !slot->normals || !slot->indices) {
        free_slot_(slot);
        return;
    }

    memcpy(slot->positions, positions, pos_bytes);
    memcpy(slot->normals, normals, nrm_bytes);
    memcpy(slot->indices, indices, idx_bytes);
    slot->vertex_count = vertex_count;
    slot->index_count = index_count;
}

void snap_mesh_cache_remove(snap_mesh_cache_t *cache, uint32_t entity_id) {
    if (!cache || !cache->meshes) return;
    if (entity_id >= cache->capacity) return;
    free_slot_(&cache->meshes[entity_id]);
}
