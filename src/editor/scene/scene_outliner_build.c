/**
 * @file scene_outliner_build.c
 * @brief Build flat outliner display list from entity attrs hierarchy.
 *
 * Rebuilds the LCRS tree from SCRIPT_KEY_PARENT_ID entity attributes
 * each frame, then performs DFS to create the flat display list.
 * Entities with skeletons have their bones injected as pseudo-children.
 *
 * Non-static functions (2 / 4 limit):
 *   scene_outliner_build
 *   scene_outliner_toggle_expand
 */

#include "ferrum/editor/scene/scene_outliner.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_scene_tree.h"
#include "ferrum/editor/edit_skeleton_registry.h"
#include "ferrum/entity/entity_attrs.h"

#include <string.h>

/**
 * @brief Rebuild the LCRS tree from entity PARENT_ID attributes.
 */
static void rebuild_tree_from_attrs_(edit_scene_tree_t *tree,
                                      const edit_entity_store_t *store) {
    for (uint32_t i = 0; i < tree->capacity; i++) {
        tree->parent[i]       = EDIT_SCENE_TREE_NONE;
        tree->first_child[i]  = EDIT_SCENE_TREE_NONE;
        tree->next_sibling[i] = EDIT_SCENE_TREE_NONE;
    }

    for (uint32_t i = 0; i < store->capacity; i++) {
        const edit_entity_t *ent = edit_entity_store_get(store, i);
        if (!ent || !ent->active) continue;

        uint8_t at = 0, as = 0;
        const void *pv = entity_attrs_get(&ent->attrs,
                                            SCRIPT_KEY_PARENT_ID, &at, &as);
        if (pv && at == SCRIPT_ATTR_U32 && as >= sizeof(uint32_t)) {
            uint32_t parent_id = *(const uint32_t *)pv;
            if (parent_id != EDIT_SCENE_TREE_NONE &&
                parent_id < store->capacity) {
                const edit_entity_t *par =
                    edit_entity_store_get(store, parent_id);
                if (par && par->active) {
                    edit_scene_tree_attach(tree, i, parent_id);
                }
            }
        }
    }
}

/**
 * @brief Inject bone rows for an entity with a skeleton.
 *
 * Looks up the entity's SKEL_PATH attribute, finds the skeleton in
 * the registry, and appends one entry per bone at depth+1.
 */
static uint32_t inject_bones_(scene_outliner_entry_t *entries,
                                uint32_t max_entries,
                                uint32_t write_pos,
                                const edit_entity_store_t *store,
                                const edit_skeleton_registry_t *skel_reg,
                                uint32_t entity_id, uint32_t depth) {
    if (!skel_reg) return write_pos;

    const edit_entity_t *ent = edit_entity_store_get(store, entity_id);
    if (!ent) return write_pos;

    uint8_t at = 0, as = 0;
    const void *sp = entity_attrs_get(&ent->attrs,
                                        SCRIPT_KEY_SKEL_PATH, &at, &as);
    if (!sp || at != SCRIPT_ATTR_STR) return write_pos;

    const char *spath = (const char *)sp;
    if (spath[0] == '\0') return write_pos;

    const char *fname = spath;
    for (const char *p = spath; *p; p++) {
        if (*p == '/') fname = p + 1;
    }

    const edit_skeleton_entry_t *se =
        edit_skeleton_registry_get(skel_reg, fname);
    if (!se || se->skel.joint_count == 0) return write_pos;

    for (uint32_t bi = 0; bi < se->skel.joint_count &&
         write_pos < max_entries; bi++) {
        entries[write_pos].entity_id    = entity_id;
        entries[write_pos].indent       = depth + 1;
        entries[write_pos].bone_index   = bi;
        entries[write_pos].has_children = false;
        entries[write_pos].expanded     = false;
        entries[write_pos].is_bone      = true;
        write_pos++;
    }

    return write_pos;
}

/**
 * @brief Recursive DFS to populate the flat display list.
 */
static uint32_t dfs_build_(scene_outliner_entry_t *entries,
                            uint32_t max_entries,
                            uint32_t write_pos,
                            const edit_entity_store_t *store,
                            const edit_scene_tree_t *tree,
                            const edit_skeleton_registry_t *skel_reg,
                            const bool *expanded, uint32_t exp_cap,
                            uint32_t node, uint32_t depth) {
    if (write_pos >= max_entries) return write_pos;

    const edit_entity_t *ent = edit_entity_store_get(store, node);
    if (!ent || !ent->active || ent->pending_delete) return write_pos;

    bool has_tree_children =
        edit_scene_tree_get_first_child(tree, node) != EDIT_SCENE_TREE_NONE;

    /* Check if entity has a skeleton (bones count as children). */
    bool has_skeleton = false;
    {
        uint8_t at = 0, as = 0;
        const void *sp = entity_attrs_get(&ent->attrs,
                                            SCRIPT_KEY_SKEL_PATH, &at, &as);
        if (sp && at == SCRIPT_ATTR_STR && ((const char *)sp)[0] != '\0') {
            has_skeleton = true;
        }
    }

    bool has_children = has_tree_children || has_skeleton;
    bool is_expanded = (node < exp_cap && expanded) ? expanded[node] : true;

    entries[write_pos].entity_id    = node;
    entries[write_pos].indent       = depth;
    entries[write_pos].bone_index   = UINT32_MAX;
    entries[write_pos].has_children = has_children;
    entries[write_pos].expanded     = is_expanded;
    entries[write_pos].is_bone      = false;
    write_pos++;

    if (has_children && is_expanded) {
        /* Inject bone rows if entity has a skeleton. */
        if (has_skeleton) {
            write_pos = inject_bones_(entries, max_entries, write_pos,
                                        store, skel_reg, node, depth);
        }

        /* Visit tree children. */
        uint32_t child = edit_scene_tree_get_first_child(tree, node);
        while (child != EDIT_SCENE_TREE_NONE && write_pos < max_entries) {
            write_pos = dfs_build_(entries, max_entries, write_pos,
                                    store, tree, skel_reg,
                                    expanded, exp_cap,
                                    child, depth + 1);
            child = edit_scene_tree_get_next_sibling(tree, child);
        }
    }

    return write_pos;
}

uint32_t scene_outliner_build(scene_outliner_entry_t *entries,
                               const edit_entity_store_t *store,
                               const edit_skeleton_registry_t *skel_reg,
                               const bool *expanded, uint32_t exp_cap) {
    if (!entries || !store || !store->tree) return 0;

    edit_scene_tree_t *tree = store->tree;

    /* Rebuild tree from entity attrs so it matches server state. */
    rebuild_tree_from_attrs_(tree, store);

    uint32_t write_pos = 0;

    /* DFS from root entities only. */
    for (uint32_t i = 0; i < store->capacity &&
         write_pos < SCENE_OUTLINER_MAX_ENTRIES; i++) {
        const edit_entity_t *ent = edit_entity_store_get(store, i);
        if (!ent || !ent->active || ent->pending_delete) continue;

        if (!edit_scene_tree_is_root(tree, i)) continue;

        write_pos = dfs_build_(entries, SCENE_OUTLINER_MAX_ENTRIES,
                                write_pos, store, tree, skel_reg,
                                expanded, exp_cap, i, 0);
    }

    return write_pos;
}

void scene_outliner_toggle_expand(bool *expanded, uint32_t exp_cap,
                                    uint32_t entity_id) {
    if (!expanded || entity_id >= exp_cap) return;
    expanded[entity_id] = !expanded[entity_id];
}
