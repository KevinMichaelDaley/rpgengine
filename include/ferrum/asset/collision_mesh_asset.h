/**
 * @file collision_mesh_asset.h
 * @brief Per-entity collision mesh FVMA binary storage.
 *
 * Stores collision mesh data (FVMA binary blobs) separately from render
 * meshes. Each entity may have at most one collision mesh. When present,
 * it overrides the render mesh for physics and snap operations.
 *
 * The store is a flat array indexed by entity ID, parallel to the entity
 * store. Each entry holds a heap-allocated copy of the FVMA binary.
 *
 * Thread safety: not thread-safe. Caller must synchronize access.
 * Ownership: the store owns all FVMA data blobs (heap-allocated).
 * Nullability: all pointer parameters must be non-NULL unless documented.
 * Error semantics: set returns false on failure; get returns NULL if absent.
 *
 * Public types: collision_mesh_entry_t, collision_mesh_store_t (2-type rule).
 */
#ifndef FERRUM_ASSET_COLLISION_MESH_ASSET_H
#define FERRUM_ASSET_COLLISION_MESH_ASSET_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* ------------------------------------------------------------------ */
/* Types                                                               */
/* ------------------------------------------------------------------ */

/**
 * @brief A single collision mesh entry (FVMA binary blob).
 *
 * When data is NULL, no collision mesh is stored for this entity.
 */
typedef struct collision_mesh_entry {
    uint8_t *data;  /**< Heap-allocated FVMA binary. NULL if empty. */
    size_t   size;  /**< FVMA data size in bytes. 0 if empty. */
} collision_mesh_entry_t;

/**
 * @brief Per-entity collision mesh store.
 *
 * Flat array indexed by entity ID. Capacity is fixed at init time.
 */
typedef struct collision_mesh_store {
    collision_mesh_entry_t *entries;   /**< Array of entries. */
    uint32_t                capacity; /**< Max entity ID + 1. */
} collision_mesh_store_t;

/* ------------------------------------------------------------------ */
/* Lifecycle (collision_mesh_store.c)                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the collision mesh store.
 *
 * Allocates space for `capacity` entries, all initially empty.
 *
 * @param store     Store to initialize. Must not be NULL.
 * @param capacity  Maximum number of entities (entity IDs 0..capacity-1).
 */
void collision_mesh_store_init(collision_mesh_store_t *store,
                                uint32_t capacity);

/**
 * @brief Destroy the store and free all owned FVMA data.
 * @param store  Store to destroy. Safe to call on zeroed struct.
 */
void collision_mesh_store_destroy(collision_mesh_store_t *store);

/**
 * @brief Remove all collision meshes without changing capacity.
 * @param store  Store to clear. Must not be NULL.
 */
void collision_mesh_store_clear(collision_mesh_store_t *store);

/* ------------------------------------------------------------------ */
/* Operations (collision_mesh_store_ops.c)                              */
/* ------------------------------------------------------------------ */

/**
 * @brief Store collision mesh FVMA data for an entity.
 *
 * Makes a heap copy of the data. If the entity already has a collision
 * mesh, the old data is freed and replaced.
 *
 * @param store      Store. Must not be NULL.
 * @param entity_id  Entity ID (must be < capacity).
 * @param data       FVMA binary data. Must not be NULL.
 * @param size       Data size in bytes. Must be > 0.
 * @return true on success, false on invalid args or allocation failure.
 *
 * Ownership: data is copied; caller retains ownership of the original.
 */
bool collision_mesh_store_set(collision_mesh_store_t *store,
                               uint32_t entity_id,
                               const uint8_t *data, size_t size);

/**
 * @brief Get the collision mesh FVMA data for an entity.
 *
 * @param store      Store. Must not be NULL.
 * @param entity_id  Entity ID.
 * @return Pointer to FVMA data, or NULL if no collision mesh is stored.
 *         Valid until the next set/remove/clear/destroy on this entity.
 */
const uint8_t *collision_mesh_store_get(const collision_mesh_store_t *store,
                                         uint32_t entity_id);

/**
 * @brief Get the FVMA data size for an entity's collision mesh.
 *
 * @param store      Store. Must not be NULL.
 * @param entity_id  Entity ID.
 * @return Size in bytes, or 0 if no collision mesh is stored.
 */
size_t collision_mesh_store_get_size(const collision_mesh_store_t *store,
                                      uint32_t entity_id);

/**
 * @brief Check if an entity has a collision mesh stored.
 *
 * @param store      Store.
 * @param entity_id  Entity ID.
 * @return true if a collision mesh is stored for this entity.
 */
bool collision_mesh_store_has(const collision_mesh_store_t *store,
                               uint32_t entity_id);

/* ------------------------------------------------------------------ */
/* Disk I/O (collision_mesh_store_io.c)                                */
/* ------------------------------------------------------------------ */

/**
 * @brief Remove the collision mesh for an entity.
 *
 * Frees the FVMA data. Safe to call if no mesh is stored.
 *
 * @param store      Store.
 * @param entity_id  Entity ID.
 */
void collision_mesh_store_remove(collision_mesh_store_t *store,
                                  uint32_t entity_id);

/**
 * @brief Save one entity's collision mesh to disk.
 *
 * Writes the FVMA binary to `<dir>/<entity_id>.fvma`.
 *
 * @param store      Store.
 * @param entity_id  Entity ID.
 * @param dir        Directory path (must exist).
 * @return true on success, false if no mesh or write error.
 */
bool collision_mesh_store_save_entry(const collision_mesh_store_t *store,
                                      uint32_t entity_id,
                                      const char *dir);

/**
 * @brief Load one entity's collision mesh from disk.
 *
 * Reads `<dir>/<entity_id>.fvma` into the store.
 *
 * @param store      Store.
 * @param entity_id  Entity ID (must be < capacity).
 * @param dir        Directory path.
 * @return true on success, false if file not found or read error.
 */
bool collision_mesh_store_load_entry(collision_mesh_store_t *store,
                                      uint32_t entity_id,
                                      const char *dir);

/**
 * @brief Save all collision meshes to disk.
 *
 * Writes each non-empty entry to `<dir>/<entity_id>.fvma`.
 *
 * @param store  Store.
 * @param dir    Directory path (must exist).
 * @return Number of entries saved.
 */
uint32_t collision_mesh_store_save_all(const collision_mesh_store_t *store,
                                        const char *dir);

/**
 * @brief Load all collision meshes from disk.
 *
 * Scans `dir` for `<N>.fvma` files and loads them into the store.
 *
 * @param store  Store.
 * @param dir    Directory path.
 * @return Number of entries loaded.
 */
uint32_t collision_mesh_store_load_all(collision_mesh_store_t *store,
                                        const char *dir);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ASSET_COLLISION_MESH_ASSET_H */
