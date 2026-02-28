/**
 * @file mesh_snapshot_update.c
 * @brief Snapshot cache update/query — serialize slot to cache, lookup.
 *
 * Non-static functions: update, get (2 of 4).
 */
#include "ferrum/editor/mesh/mesh_snapshot.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Internal: FNV-1a 64-bit hash                                        */
/* ------------------------------------------------------------------ */

static uint64_t fnv1a_hash_(const uint8_t *data, size_t len) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (size_t i = 0; i < len; i++) {
        hash ^= (uint64_t)data[i];
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

bool mesh_snapshot_cache_update(mesh_snapshot_cache_t *cache,
                                uint32_t slot_idx,
                                const mesh_slot_t *slot,
                                uint32_t flags) {
    if (!cache || !cache->entries || !slot) { return false; }
    if (slot_idx >= MESH_MAX_EDITABLE) { return false; }

    /* Compute serialized size */
    size_t size = mesh_vao_serialized_size(slot, flags);
    if (size == 0) { return false; }

    /* Allocate new buffer */
    uint8_t *buf = malloc(size);
    if (!buf) { return false; }

    /* Serialize */
    size_t written = mesh_vao_serialize(slot, flags, buf, size);
    if (written != size) {
        free(buf);
        return false;
    }

    /* Replace existing entry */
    mesh_snapshot_entry_t *entry = &cache->entries[slot_idx];
    free(entry->data);
    entry->data = buf;
    entry->size = size;
    entry->hash = fnv1a_hash_(buf, size);

    return true;
}

bool mesh_snapshot_cache_get(const mesh_snapshot_cache_t *cache,
                             uint32_t slot_idx,
                             const uint8_t **data,
                             size_t *size,
                             uint64_t *hash) {
    if (!cache || !cache->entries) { return false; }
    if (slot_idx >= MESH_MAX_EDITABLE) { return false; }

    const mesh_snapshot_entry_t *entry = &cache->entries[slot_idx];
    if (!entry->data) { return false; }

    if (data) { *data = entry->data; }
    if (size) { *size = entry->size; }
    if (hash) { *hash = entry->hash; }
    return true;
}
