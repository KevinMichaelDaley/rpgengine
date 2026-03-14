/**
 * @file edit_entity_store.c
 * @brief Entity store lifecycle and creation/removal with O(1) freelist.
 *
 * The entity array is allocated via mmap (POSIX) or VirtualAlloc (Windows)
 * so the OS only commits physical pages on first touch. This allows a large
 * virtual reservation (e.g. 1M slots ≈ 1.5 GB virtual) without consuming
 * real memory until entities are actually created.
 */

#include "ferrum/editor/edit_entity.h"
#include "ferrum/memory/vm_alloc.h"
#include <stdlib.h>
#include <string.h>

bool edit_entity_store_init(edit_entity_store_t *store, uint32_t capacity) {
    if (!store || capacity == 0) return false;

    size_t ent_bytes = (size_t)capacity * sizeof(edit_entity_t);
    store->entities = (edit_entity_t *)vm_reserve(ent_bytes);
    if (!store->entities) return false;
    store->entities_bytes = ent_bytes;

    store->freelist = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    if (!store->freelist) {
        vm_release(store->entities, ent_bytes);
        store->entities = NULL;
        return false;
    }
    store->capacity = capacity;
    store->active_count = 0;
    /* Populate freelist so that index 0 is popped first (top of stack). */
    store->free_count = capacity;
    for (uint32_t i = 0; i < capacity; i++) {
        store->freelist[i] = capacity - 1 - i;
    }
    return true;
}

void edit_entity_store_destroy(edit_entity_store_t *store) {
    if (!store) return;
    vm_release(store->entities, store->entities_bytes);
    free(store->freelist);
    store->entities = NULL;
    store->freelist = NULL;
    store->capacity = 0;
    store->entities_bytes = 0;
    store->free_count = 0;
    store->active_count = 0;
}

uint32_t edit_entity_store_create(edit_entity_store_t *store, uint32_t type) {
    if (!store || store->free_count == 0) return EDIT_ENTITY_INVALID_ID;
    /* Pop from freelist stack. */
    uint32_t i = store->freelist[--store->free_count];
    edit_entity_t *e = &store->entities[i];
    memset(e, 0, sizeof(*e));
    e->pos[0] = e->pos[1] = e->pos[2] = 0.0f;
    e->rot[0] = e->rot[1] = e->rot[2] = 0.0f;
    e->scale[0] = e->scale[1] = e->scale[2] = 1.0f;
    e->orientation = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    e->type = type;
    e->body_index = EDIT_ENTITY_INVALID_ID;
    e->active = true;
    store->active_count++;
    return i;
}

bool edit_entity_store_remove(edit_entity_store_t *store, uint32_t id) {
    if (!store || id >= store->capacity) return false;
    if (!store->entities[id].active) return false;
    store->entities[id].active = false;
    /* Push back onto freelist stack. */
    store->freelist[store->free_count++] = id;
    store->active_count--;
    return true;
}
