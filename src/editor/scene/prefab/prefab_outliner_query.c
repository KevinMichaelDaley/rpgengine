/**
 * @file prefab_outliner_query.c
 * @brief Query functions for the prefab outliner tree.
 *
 * Non-static functions: prefab_outliner_count, prefab_outliner_get (2/4).
 */

#include "ferrum/editor/scene/prefab/prefab_outliner.h"

#include <stddef.h>

uint32_t prefab_outliner_count(const prefab_outliner_t *tree) {
    if (!tree) return 0;
    return tree->count;
}

const prefab_outliner_entry_t *prefab_outliner_get(
    const prefab_outliner_t *tree, uint32_t index) {
    if (!tree || index >= tree->count) return NULL;
    return &tree->entries[index];
}
