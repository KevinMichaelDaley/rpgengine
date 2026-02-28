/**
 * @file edit_asset_registry.c
 * @brief Asset registry core — init, destroy, add, find, list, search, complete.
 *
 * Non-static functions: 4 (init, destroy, add, count).
 * Query functions are in edit_asset_query.c.
 */

#include "ferrum/editor/edit_asset_registry.h"

#include <stdlib.h>
#include <string.h>

void edit_asset_registry_init(edit_asset_registry_t *reg, uint32_t capacity) {
    if (!reg) return;
    reg->entries = calloc(capacity, sizeof(edit_asset_entry_t));
    reg->count = 0;
    reg->capacity = capacity;
}

void edit_asset_registry_destroy(edit_asset_registry_t *reg) {
    if (!reg) return;
    free(reg->entries);
    reg->entries = NULL;
    reg->count = 0;
    reg->capacity = 0;
}

bool edit_asset_registry_add(edit_asset_registry_t *reg,
                              const char *path,
                              edit_asset_type_t type,
                              uint32_t size, uint32_t hash) {
    if (!reg || !reg->entries || !path) return false;
    if (reg->count >= reg->capacity) return false;

    /* Reject duplicates. */
    if (edit_asset_registry_find(reg, path) != NULL) return false;

    edit_asset_entry_t *e = &reg->entries[reg->count];
    strncpy(e->path, path, EDIT_ASSET_PATH_MAX - 1);
    e->path[EDIT_ASSET_PATH_MAX - 1] = '\0';
    e->type = type;
    e->size = size;
    e->hash = hash;
    reg->count++;
    return true;
}

uint32_t edit_asset_registry_count(const edit_asset_registry_t *reg) {
    return reg ? reg->count : 0;
}
