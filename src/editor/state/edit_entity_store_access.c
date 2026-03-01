/**
 * @file edit_entity_store_access.c
 * @brief Entity store access and query operations.
 */

#include "ferrum/editor/edit_entity.h"
#include <string.h>

const edit_entity_t *edit_entity_store_get(const edit_entity_store_t *store,
                                            uint32_t id) {
    if (!store || id >= store->capacity) return NULL;
    if (!store->entities[id].active) return NULL;
    return &store->entities[id];
}

edit_entity_t *edit_entity_store_get_mut(edit_entity_store_t *store,
                                          uint32_t id) {
    if (!store || id >= store->capacity) return NULL;
    if (!store->entities[id].active) return NULL;
    return &store->entities[id];
}

bool edit_entity_store_restore(edit_entity_store_t *store, uint32_t id,
                                const edit_entity_t *snapshot) {
    if (!store || !snapshot || id >= store->capacity) return false;
    if (store->entities[id].active) return false;
    memcpy(&store->entities[id], snapshot, sizeof(edit_entity_t));
    store->entities[id].active = true;
    /* Remove id from freelist (swap-remove). Restore is rare (undo only). */
    for (uint32_t i = 0; i < store->free_count; i++) {
        if (store->freelist[i] == id) {
            store->freelist[i] = store->freelist[--store->free_count];
            break;
        }
    }
    store->active_count++;
    return true;
}

uint32_t edit_entity_store_count(const edit_entity_store_t *store) {
    if (!store) return 0;
    return store->active_count;
}
