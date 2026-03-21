/**
 * @file edit_scene_tree.h
 * @brief LCRS (left-child right-sibling) scene tree for entity hierarchy.
 *
 * Parallel arrays indexed by entity ID. Provides attach, detach,
 * ancestry queries, and depth-first iteration. Used by the outliner
 * to display hierarchical entity trees with expand/collapse.
 *
 * Thread safety: must be called from the main tick thread only.
 */
#ifndef FERRUM_EDITOR_EDIT_SCENE_TREE_H
#define FERRUM_EDITOR_EDIT_SCENE_TREE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/** @brief Sentinel value for "no node" (root / no child / no sibling). */
#define EDIT_SCENE_TREE_NONE UINT32_MAX

/* ------------------------------------------------------------------------ */
/* Types                                                                     */
/* ------------------------------------------------------------------------ */

/**
 * @brief LCRS scene tree — parallel arrays indexed by entity ID.
 *
 * Ownership: init() allocates; destroy() frees.
 */
typedef struct edit_scene_tree {
    uint32_t *parent;        /**< Parent entity ID (NONE = root). */
    uint32_t *first_child;   /**< First child entity ID (NONE = leaf). */
    uint32_t *next_sibling;  /**< Next sibling entity ID (NONE = last). */
    uint32_t  capacity;      /**< Max entity ID + 1. */
    uint32_t  version;       /**< Bumped on every structural change. */
} edit_scene_tree_t;

/**
 * @brief DFS iterator over a subtree.
 *
 * Stack-based depth-first traversal. Initialize with iter_init,
 * then call iter_next repeatedly until it returns false.
 */
typedef struct edit_scene_tree_iter {
    const edit_scene_tree_t *tree;
    uint32_t stack[256];     /**< Node stack (entity IDs). */
    uint32_t depths[256];    /**< Depth stack (parallel to node stack). */
    uint32_t top;            /**< Stack pointer. */
} edit_scene_tree_iter_t;

/* ------------------------------------------------------------------------ */
/* Lifecycle (edit_scene_tree.c)                                              */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize the scene tree with given capacity.
 * @param tree      Tree to initialize.
 * @param capacity  Max number of entities (array size).
 * @return true on success, false on allocation failure or NULL.
 */
bool edit_scene_tree_init(edit_scene_tree_t *tree, uint32_t capacity);

/**
 * @brief Free all memory owned by the tree.
 * @param tree  Tree to destroy (NULL is safe).
 */
void edit_scene_tree_destroy(edit_scene_tree_t *tree);

/**
 * @brief Attach child to parent in the LCRS tree.
 *
 * If child is already attached elsewhere, it is detached first.
 * Rejects self-attachment and circular references.
 *
 * @param tree    Scene tree.
 * @param child   Entity ID to attach.
 * @param parent  Entity ID to attach under.
 * @return true on success, false if invalid (self, circular, out of range).
 */
bool edit_scene_tree_attach(edit_scene_tree_t *tree,
                             uint32_t child, uint32_t parent);

/**
 * @brief Detach an entity from its parent (becomes a root).
 *
 * Children of the detached entity remain attached to it.
 *
 * @param tree    Scene tree.
 * @param entity  Entity ID to detach.
 */
void edit_scene_tree_detach(edit_scene_tree_t *tree, uint32_t entity);

/* ------------------------------------------------------------------------ */
/* Query (edit_scene_tree_query.c)                                            */
/* ------------------------------------------------------------------------ */

/** @brief Get parent of entity (NONE if root). */
uint32_t edit_scene_tree_get_parent(const edit_scene_tree_t *tree,
                                     uint32_t entity);

/** @brief Get first child of entity (NONE if leaf). */
uint32_t edit_scene_tree_get_first_child(const edit_scene_tree_t *tree,
                                          uint32_t entity);

/** @brief Get next sibling of entity (NONE if last). */
uint32_t edit_scene_tree_get_next_sibling(const edit_scene_tree_t *tree,
                                           uint32_t entity);

/** @brief True if entity has no parent (is a root node). */
bool edit_scene_tree_is_root(const edit_scene_tree_t *tree, uint32_t entity);

/**
 * @brief Check if ancestor is an ancestor of descendant.
 * @return true if ancestor appears in the parent chain of descendant.
 */
bool edit_scene_tree_is_ancestor(const edit_scene_tree_t *tree,
                                  uint32_t ancestor, uint32_t descendant);

/**
 * @brief Count all descendants of an entity (recursive).
 * @return Number of descendants (0 for leaf nodes).
 */
uint32_t edit_scene_tree_count_descendants(const edit_scene_tree_t *tree,
                                            uint32_t entity);

/* ------------------------------------------------------------------------ */
/* Iteration (edit_scene_tree_iter.c)                                         */
/* ------------------------------------------------------------------------ */

/**
 * @brief Initialize a DFS iterator starting at the given root.
 * @param it    Iterator to initialize.
 * @param tree  Tree to iterate.
 * @param root  Starting entity ID.
 */
void edit_scene_tree_iter_init(edit_scene_tree_iter_t *it,
                                const edit_scene_tree_t *tree,
                                uint32_t root);

/**
 * @brief Advance the DFS iterator.
 *
 * @param it        Iterator.
 * @param out_node  Output: entity ID of next node.
 * @param out_depth Output: depth relative to root (0 = root itself).
 * @return true if a node was produced, false if iteration complete.
 */
bool edit_scene_tree_iter_next(edit_scene_tree_iter_t *it,
                                uint32_t *out_node, uint32_t *out_depth);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_EDIT_SCENE_TREE_H */
