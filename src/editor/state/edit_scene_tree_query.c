/**
 * @file edit_scene_tree_query.c
 * @brief LCRS scene tree query operations.
 *
 * Non-static functions (4 / 4 limit):
 *   edit_scene_tree_get_parent
 *   edit_scene_tree_is_root
 *   edit_scene_tree_is_ancestor
 *   edit_scene_tree_count_descendants
 */

#include "ferrum/editor/edit_scene_tree.h"

#define NONE EDIT_SCENE_TREE_NONE

uint32_t edit_scene_tree_get_parent(const edit_scene_tree_t *tree,
                                     uint32_t entity) {
    if (!tree || entity >= tree->capacity) return NONE;
    return tree->parent[entity];
}

bool edit_scene_tree_is_root(const edit_scene_tree_t *tree, uint32_t entity) {
    if (!tree || entity >= tree->capacity) return true;
    return tree->parent[entity] == NONE;
}

bool edit_scene_tree_is_ancestor(const edit_scene_tree_t *tree,
                                  uint32_t ancestor, uint32_t descendant) {
    if (!tree || ancestor >= tree->capacity || descendant >= tree->capacity) {
        return false;
    }
    if (ancestor == descendant) return false;

    /* Walk up from descendant looking for ancestor. */
    uint32_t cur = tree->parent[descendant];
    while (cur != NONE) {
        if (cur == ancestor) return true;
        cur = tree->parent[cur];
    }
    return false;
}

uint32_t edit_scene_tree_count_descendants(const edit_scene_tree_t *tree,
                                            uint32_t entity) {
    if (!tree || entity >= tree->capacity) return 0;

    /* Iterative DFS count using the LCRS structure. */
    uint32_t count = 0;
    uint32_t stack[256];
    uint32_t top = 0;

    /* Push all direct children. */
    uint32_t child = tree->first_child[entity];
    while (child != NONE && top < 256) {
        stack[top++] = child;
        child = tree->next_sibling[child];
    }

    while (top > 0) {
        uint32_t node = stack[--top];
        count++;

        /* Push this node's children. */
        child = tree->first_child[node];
        while (child != NONE && top < 256) {
            stack[top++] = child;
            child = tree->next_sibling[child];
        }
    }

    return count;
}
