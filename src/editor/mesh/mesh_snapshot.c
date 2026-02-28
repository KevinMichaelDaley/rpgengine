/**
 * @file mesh_snapshot.c
 * @brief Snapshot cache lifecycle — init, destroy.
 *
 * Non-static functions: init, destroy (2 of 4).
 */
#include "ferrum/editor/mesh/mesh_snapshot.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_snapshot_cache_init(mesh_snapshot_cache_t *cache) {
    if (!cache) { return false; }

    cache->entries = calloc(MESH_MAX_EDITABLE, sizeof(mesh_snapshot_entry_t));
    if (!cache->entries) { return false; }

    return true;
}

void mesh_snapshot_cache_destroy(mesh_snapshot_cache_t *cache) {
    if (!cache || !cache->entries) { return; }

    for (uint32_t i = 0; i < MESH_MAX_EDITABLE; i++) {
        free(cache->entries[i].data);
    }
    free(cache->entries);
    cache->entries = NULL;
}
