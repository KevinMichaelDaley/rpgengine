/**
 * @file edit_scene_tree.c
 * @brief LCRS scene tree lifecycle and structural operations.
 *
 * Non-static functions (4 / 4 limit):
 *   edit_scene_tree_init
 *   edit_scene_tree_destroy
 *   edit_scene_tree_attach
 *   edit_scene_tree_detach
 */

#include "ferrum/editor/edit_scene_tree.h"
#include <stdlib.h>
#include <string.h>

#define NONE EDIT_SCENE_TREE_NONE

bool edit_scene_tree_init(edit_scene_tree_t *tree, uint32_t capacity) {
    if (!tree || capacity == 0) return false;
    memset(tree, 0, sizeof(*tree));

    tree->parent       = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    tree->first_child  = (uint32_t *)malloc(capacity * sizeof(uint32_t));
    tree->next_sibling = (uint32_t *)malloc(capacity * sizeof(uint32_t));

    if (!tree->parent || !tree->first_child || !tree->next_sibling) {
        free(tree->parent);
        free(tree->first_child);
        free(tree->next_sibling);
        memset(tree, 0, sizeof(*tree));
        return false;
    }

    tree->capacity = capacity;
    tree->version  = 0;

    /* All nodes start as roots with no children or siblings. */
    for (uint32_t i = 0; i < capacity; i++) {
        tree->parent[i]       = NONE;
        tree->first_child[i]  = NONE;
        tree->next_sibling[i] = NONE;
    }

    return true;
}

void edit_scene_tree_destroy(edit_scene_tree_t *tree) {
    if (!tree) return;
    free(tree->parent);
    free(tree->first_child);
    free(tree->next_sibling);
    memset(tree, 0, sizeof(*tree));
}

bool edit_scene_tree_attach(edit_scene_tree_t *tree,
                             uint32_t child, uint32_t parent) {
    if (!tree) return false;
    if (child >= tree->capacity || parent >= tree->capacity) return false;
    if (child == parent) return false;

    /* Reject circular: parent must not be a descendant of child. */
    if (edit_scene_tree_is_ancestor(tree, child, parent)) return false;

    /* If already attached somewhere, detach first. */
    if (tree->parent[child] != NONE) {
        edit_scene_tree_detach(tree, child);
    }

    /* Prepend child to parent's child list. */
    tree->next_sibling[child] = tree->first_child[parent];
    tree->first_child[parent] = child;
    tree->parent[child]       = parent;

    tree->version++;
    return true;
}

void edit_scene_tree_detach(edit_scene_tree_t *tree, uint32_t entity) {
    if (!tree || entity >= tree->capacity) return;

    uint32_t par = tree->parent[entity];
    if (par == NONE) return; /* Already a root. */

    /* Unlink from parent's child list. */
    if (tree->first_child[par] == entity) {
        /* Entity is the first child — advance first_child pointer. */
        tree->first_child[par] = tree->next_sibling[entity];
    } else {
        /* Walk siblings to find the one before entity. */
        uint32_t prev = tree->first_child[par];
        while (prev != NONE && tree->next_sibling[prev] != entity) {
            prev = tree->next_sibling[prev];
        }
        if (prev != NONE) {
            tree->next_sibling[prev] = tree->next_sibling[entity];
        }
    }

    tree->parent[entity]       = NONE;
    tree->next_sibling[entity] = NONE;

    tree->version++;
}
