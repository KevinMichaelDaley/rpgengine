/**
 * @file edit_entity_store_query.c
 * @brief Entity store query — find by name.
 *
 * Non-static functions: edit_entity_store_find_by_name (1).
 */

#include "ferrum/editor/edit_entity.h"
#include <string.h>

uint32_t edit_entity_store_find_by_name(const edit_entity_store_t *store,
                                        const char *name) {
    if (!store || !name || name[0] == '\0') return EDIT_ENTITY_INVALID_ID;
    for (uint32_t i = 0; i < store->capacity; i++) {
        if (store->entities[i].active &&
            store->entities[i].name[0] != '\0' &&
            strcmp(store->entities[i].name, name) == 0) {
            return i;
        }
    }
    return EDIT_ENTITY_INVALID_ID;
}
