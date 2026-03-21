/**
 * @file edit_skeleton_registry_query.c
 * @brief Skeleton registry mutable query.
 *
 * Non-static functions (1 / 4-function rule):
 *   1. edit_skeleton_registry_get_mut
 */

#include "ferrum/editor/edit_skeleton_registry.h"
#include <string.h>

edit_skeleton_entry_t *edit_skeleton_registry_get_mut(
    edit_skeleton_registry_t *reg, const char *path) {
    if (!reg || !path) return NULL;

    for (uint32_t i = 0; i < reg->capacity; i++) {
        if (reg->entries[i].active &&
            strcmp(reg->entries[i].path, path) == 0) {
            return &reg->entries[i];
        }
    }
    return NULL;
}
