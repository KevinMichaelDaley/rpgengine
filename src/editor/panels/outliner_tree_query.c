/**
 * @file outliner_tree_query.c
 * @brief Outliner tree query and scroll functions.
 *
 * Non-static functions: 3 (count, get, scroll).
 */

#include "ferrum/editor/panels/outliner_tree.h"
#include <stddef.h>

uint32_t outliner_tree_count(const outliner_tree_t *tree) {
    return tree->entry_count;
}

const outliner_entry_t *outliner_tree_get(const outliner_tree_t *tree,
                                           uint32_t index) {
    if (index >= tree->entry_count) return NULL;
    return &tree->entries[index];
}

void outliner_tree_scroll(outliner_tree_t *tree, int delta) {
    int new_offset = tree->scroll_offset + delta;
    if (new_offset < 0) new_offset = 0;
    tree->scroll_offset = new_offset;
}
