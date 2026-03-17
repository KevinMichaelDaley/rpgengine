/**
 * @file collision_mesh_store_ops.c
 * @brief Collision mesh store operations: set, get, get_size, has.
 *
 * Non-static functions (4 / 4 limit):
 *   collision_mesh_store_set
 *   collision_mesh_store_get
 *   collision_mesh_store_get_size
 *   collision_mesh_store_has
 */

#include "ferrum/asset/collision_mesh_asset.h"

#include <stdlib.h>
#include <string.h>

bool collision_mesh_store_set(collision_mesh_store_t *store,
                               uint32_t entity_id,
                               const uint8_t *data, size_t size) {
    if (!store || !data || size == 0) return false;
    if (!store->entries) return false;
    if (entity_id >= store->capacity) return false;

    /* Allocate and copy the FVMA data. */
    uint8_t *copy = (uint8_t *)malloc(size);
    if (!copy) return false;
    memcpy(copy, data, size);

    /* Free any existing data for this entity. */
    free(store->entries[entity_id].data);

    store->entries[entity_id].data = copy;
    store->entries[entity_id].size = size;
    return true;
}

const uint8_t *collision_mesh_store_get(const collision_mesh_store_t *store,
                                         uint32_t entity_id) {
    if (!store || !store->entries) return NULL;
    if (entity_id >= store->capacity) return NULL;
    return store->entries[entity_id].data;
}

size_t collision_mesh_store_get_size(const collision_mesh_store_t *store,
                                      uint32_t entity_id) {
    if (!store || !store->entries) return 0;
    if (entity_id >= store->capacity) return 0;
    return store->entries[entity_id].size;
}

bool collision_mesh_store_has(const collision_mesh_store_t *store,
                               uint32_t entity_id) {
    if (!store || !store->entries) return false;
    if (entity_id >= store->capacity) return false;
    return store->entries[entity_id].data != NULL;
}
