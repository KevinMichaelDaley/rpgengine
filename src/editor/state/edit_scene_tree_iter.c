/**
 * @file edit_scene_tree_iter.c
 * @brief LCRS scene tree DFS iterator and accessors.
 *
 * Non-static functions (4 / 4 limit):
 *   edit_scene_tree_iter_init
 *   edit_scene_tree_iter_next
 *   edit_scene_tree_get_first_child
 *   edit_scene_tree_get_next_sibling
 */

#include "ferrum/editor/edit_scene_tree.h"

#define NONE EDIT_SCENE_TREE_NONE

uint32_t edit_scene_tree_get_first_child(const edit_scene_tree_t *tree,
                                          uint32_t entity) {
    if (!tree || entity >= tree->capacity) return NONE;
    return tree->first_child[entity];
}

uint32_t edit_scene_tree_get_next_sibling(const edit_scene_tree_t *tree,
                                           uint32_t entity) {
    if (!tree || entity >= tree->capacity) return NONE;
    return tree->next_sibling[entity];
}

void edit_scene_tree_iter_init(edit_scene_tree_iter_t *it,
                                const edit_scene_tree_t *tree,
                                uint32_t root) {
    if (!it) return;
    it->tree = tree;
    it->top  = 0;

    if (tree && root < tree->capacity) {
        it->stack[0]  = root;
        it->depths[0] = 0;
        it->top       = 1;
    }
}

bool edit_scene_tree_iter_next(edit_scene_tree_iter_t *it,
                                uint32_t *out_node, uint32_t *out_depth) {
    if (!it || it->top == 0 || !it->tree) return false;

    /* Pop the top of the stack. */
    it->top--;
    uint32_t node  = it->stack[it->top];
    uint32_t depth = it->depths[it->top];

    if (out_node)  *out_node  = node;
    if (out_depth) *out_depth = depth;

    /* Push children in reverse order (so first_child is visited first).
     * Collect children into a temp array, then push in reverse. */
    uint32_t children[256];
    uint32_t child_count = 0;

    uint32_t c = it->tree->first_child[node];
    while (c != NONE && child_count < 256) {
        children[child_count++] = c;
        c = it->tree->next_sibling[c];
    }

    /* Push in reverse so first child is on top of stack. */
    for (uint32_t i = child_count; i > 0 && it->top < 256; i--) {
        it->stack[it->top]  = children[i - 1];
        it->depths[it->top] = depth + 1;
        it->top++;
    }

    return true;
}
