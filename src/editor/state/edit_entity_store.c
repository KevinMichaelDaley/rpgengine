/**
 * @file edit_entity_store.c
 * @brief Entity store lifecycle and creation/removal.
 */

#include "ferrum/editor/edit_entity.h"
#include <stdlib.h>
#include <string.h>

bool edit_entity_store_init(edit_entity_store_t *store, uint32_t capacity) {
    if (!store || capacity == 0) return false;
    store->entities = (edit_entity_t *)calloc(capacity, sizeof(edit_entity_t));
    if (!store->entities) return false;
    store->capacity = capacity;
    return true;
}

void edit_entity_store_destroy(edit_entity_store_t *store) {
    if (!store) return;
    free(store->entities);
    store->entities = NULL;
    store->capacity = 0;
}

uint32_t edit_entity_store_create(edit_entity_store_t *store, uint32_t type) {
    if (!store) return EDIT_ENTITY_INVALID_ID;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (!store->entities[i].active) {
            edit_entity_t *e = &store->entities[i];
            memset(e, 0, sizeof(*e));
            e->pos[0] = e->pos[1] = e->pos[2] = 0.0f;
            e->rot[0] = e->rot[1] = e->rot[2] = 0.0f;
            e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
            e->type   = type;
            e->active = true;
            return i;
        }
    }
    return EDIT_ENTITY_INVALID_ID;
}

bool edit_entity_store_remove(edit_entity_store_t *store, uint32_t id) {
    if (!store || id >= store->capacity) return false;
    if (!store->entities[id].active) return false;
    store->entities[id].active = false;
    return true;
}
