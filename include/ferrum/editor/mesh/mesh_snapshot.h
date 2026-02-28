/**
 * @file mesh_snapshot.h
 * @brief Mesh snapshot cache — serialized FVMA blobs per slot.
 *
 * Caches serialized mesh data for transfer to clients. Each slot
 * maintains a FVMA byte buffer and a content hash for change detection.
 *
 * Ownership: init() allocates, destroy() frees. Cached data is
 * owned by the cache; pointers from get() are valid until next
 * update() or destroy().
 *
 * Nullability: NULL args handled gracefully.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_SNAPSHOT_H
#define FERRUM_EDITOR_MESH_SNAPSHOT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_vao_format.h"
#include "ferrum/editor/mesh/mesh_edit.h"

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief Cached snapshot for one mesh slot.
 */
typedef struct mesh_snapshot_entry {
    uint8_t  *data;  /**< Serialized FVMA blob. NULL if empty. */
    size_t    size;   /**< Blob size in bytes. */
    uint64_t  hash;   /**< Content hash for change detection. */
} mesh_snapshot_entry_t;

/**
 * @brief Snapshot cache for all editable mesh slots.
 */
typedef struct mesh_snapshot_cache {
    mesh_snapshot_entry_t *entries; /**< Array of MESH_MAX_EDITABLE entries. */
} mesh_snapshot_cache_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle (mesh_snapshot.c)                                               */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the snapshot cache.
 * @param cache  Cache to initialize. Must not be NULL.
 * @return true on success.
 */
bool mesh_snapshot_cache_init(mesh_snapshot_cache_t *cache);

/**
 * @brief Free all cached data. NULL-safe.
 */
void mesh_snapshot_cache_destroy(mesh_snapshot_cache_t *cache);

/* ------------------------------------------------------------------------ */
/* Update / Query (mesh_snapshot_update.c)                                   */
/* ------------------------------------------------------------------------ */

/**
 * @brief Serialize a mesh slot and cache the result.
 *
 * Replaces any existing cached data for the slot. Computes a
 * content hash from the serialized data.
 *
 * @param cache  Snapshot cache.
 * @param slot_idx  Slot index [0, MESH_MAX_EDITABLE).
 * @param slot      Mesh slot to serialize.
 * @param flags     MESH_VAO_FLAG_* bitmask for serialization.
 * @return true on success.
 */
bool mesh_snapshot_cache_update(mesh_snapshot_cache_t *cache,
                                uint32_t slot_idx,
                                const mesh_slot_t *slot,
                                uint32_t flags);

/**
 * @brief Get cached snapshot data for a slot.
 *
 * @param cache     Snapshot cache.
 * @param slot_idx  Slot index.
 * @param[out] data  Pointer to cached data. Valid until next update/destroy.
 * @param[out] size  Data size in bytes.
 * @param[out] hash  Content hash.
 * @return true if slot has cached data, false if empty or OOB.
 */
bool mesh_snapshot_cache_get(const mesh_snapshot_cache_t *cache,
                             uint32_t slot_idx,
                             const uint8_t **data,
                             size_t *size,
                             uint64_t *hash);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_SNAPSHOT_H */
