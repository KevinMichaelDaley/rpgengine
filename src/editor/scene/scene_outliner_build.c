/**
 * @file scene_outliner_build.c
 * @brief Build flat outliner display list from LCRS scene tree.
 *
 * Non-static functions (2 / 4 limit):
 *   scene_outliner_build
 *   scene_outliner_toggle_expand
 */

#include "ferrum/editor/scene/scene_outliner.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_scene_tree.h"

/**
 * @brief Recursive DFS to populate the flat display list.
 *
 * Visits the subtree rooted at `node`, appending entries for active
 * entities. Skips children of collapsed nodes.
 */
static uint32_t dfs_build_(scene_outliner_entry_t *entries,
                            uint32_t max_entries,
                            uint32_t write_pos,
                            const edit_entity_store_t *store,
                            const edit_scene_tree_t *tree,
                            const bool *expanded, uint32_t exp_cap,
                            uint32_t node, uint32_t depth) {
    if (write_pos >= max_entries) return write_pos;

    const edit_entity_t *ent = edit_entity_store_get(store, node);
    if (!ent || !ent->active || ent->pending_delete) return write_pos;

    bool has_children =
        edit_scene_tree_get_first_child(tree, node) != EDIT_SCENE_TREE_NONE;
    bool is_expanded = (node < exp_cap) ? expanded[node] : true;

    entries[write_pos].entity_id    = node;
    entries[write_pos].indent       = depth;
    entries[write_pos].has_children = has_children;
    entries[write_pos].expanded     = is_expanded;
    write_pos++;

    /* If expanded, visit children. */
    if (has_children && is_expanded) {
        uint32_t child = edit_scene_tree_get_first_child(tree, node);
        while (child != EDIT_SCENE_TREE_NONE && write_pos < max_entries) {
            write_pos = dfs_build_(entries, max_entries, write_pos,
                                    store, tree, expanded, exp_cap,
                                    child, depth + 1);
            child = edit_scene_tree_get_next_sibling(tree, child);
        }
    }

    return write_pos;
}

uint32_t scene_outliner_build(scene_outliner_entry_t *entries,
                               const edit_entity_store_t *store,
                               const bool *expanded, uint32_t exp_cap) {
    if (!entries || !store || !store->tree) return 0;

    const edit_scene_tree_t *tree = store->tree;
    uint32_t write_pos = 0;

    /* Iterate all entities; start DFS from roots only. */
    for (uint32_t i = 0; i < store->capacity && write_pos < SCENE_OUTLINER_MAX_ENTRIES; i++) {
        const edit_entity_t *ent = edit_entity_store_get(store, i);
        if (!ent || !ent->active || ent->pending_delete) continue;

        /* Only start DFS from root entities (no parent). */
        if (!edit_scene_tree_is_root(tree, i)) continue;

        write_pos = dfs_build_(entries, SCENE_OUTLINER_MAX_ENTRIES,
                                write_pos, store, tree,
                                expanded, exp_cap, i, 0);
    }

    return write_pos;
}

void scene_outliner_toggle_expand(bool *expanded, uint32_t exp_cap,
                                    uint32_t entity_id) {
    if (!expanded || entity_id >= exp_cap) return;
    expanded[entity_id] = !expanded[entity_id];
}
