/**
 * @file collision_mesh_store.c
 * @brief Collision mesh store lifecycle: init, destroy, clear.
 *
 * Non-static functions (3 / 4 limit):
 *   collision_mesh_store_init
 *   collision_mesh_store_destroy
 *   collision_mesh_store_clear
 */

#include "ferrum/asset/collision_mesh_asset.h"

#include <stdlib.h>
#include <string.h>

void collision_mesh_store_init(collision_mesh_store_t *store,
                                uint32_t capacity) {
    if (!store) return;

    store->capacity = capacity;
    if (capacity == 0) {
        store->entries = NULL;
        return;
    }

    store->entries = (collision_mesh_entry_t *)calloc(
        capacity, sizeof(collision_mesh_entry_t));
}

void collision_mesh_store_destroy(collision_mesh_store_t *store) {
    if (!store) return;

    /* Free all owned FVMA blobs. */
    if (store->entries) {
        for (uint32_t i = 0; i < store->capacity; i++) {
            free(store->entries[i].data);
        }
        free(store->entries);
    }

    store->entries = NULL;
    store->capacity = 0;
}

void collision_mesh_store_clear(collision_mesh_store_t *store) {
    if (!store || !store->entries) return;

    for (uint32_t i = 0; i < store->capacity; i++) {
        free(store->entries[i].data);
        store->entries[i].data = NULL;
        store->entries[i].size = 0;
    }
}
