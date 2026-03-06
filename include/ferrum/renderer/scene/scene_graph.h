/**
 * @file scene_graph.h
 * @brief LCRS scene graph over the entity pool.
 *
 * Manages a flat-array left-child right-sibling tree stored parallel
 * to the entity pool. Provides attach, detach, dirty marking, and
 * hierarchical world-transform propagation.
 *
 * Ownership:
 * - scene_graph_init() allocates internal arrays (nodes, dirty_list).
 * - scene_graph_destroy() frees them.
 * - The caller owns the scene_graph_t struct itself.
 *
 * Nullability:
 * - All functions accepting a scene_graph_t* handle NULL gracefully
 *   (no-op or return error).
 *
 * Thread safety:
 * - Not thread-safe. External synchronization required.
 */
#ifndef FERRUM_RENDERER_SCENE_GRAPH_H
#define FERRUM_RENDERER_SCENE_GRAPH_H

#include <stdint.h>
#include "ferrum/renderer/scene/scene_node.h"

/**
 * @brief Scene graph — flat-array LCRS tree parallel to entity pool.
 */
typedef struct scene_graph {
    scene_node_t *nodes;       /**< Array of scene nodes, indexed by entity index. */
    uint32_t      capacity;    /**< Number of node slots (matches entity pool capacity). */
    uint32_t     *dirty_list;  /**< Indices of dirty root nodes for BFS update. */
    uint32_t      dirty_count; /**< Number of entries in dirty_list. */
} scene_graph_t;

/**
 * @brief Initialize a scene graph with the given capacity.
 *
 * Allocates the node array and dirty list. All nodes start unattached.
 *
 * @param graph    Non-null pointer to scene graph to initialize.
 * @param capacity Number of entity slots (must be > 0).
 * @return 0 on success, -1 on invalid args or allocation failure.
 */
int scene_graph_init(scene_graph_t *graph, uint32_t capacity);

/**
 * @brief Destroy a scene graph, freeing internal arrays.
 *
 * Safe to call with NULL. After destruction, the graph is zeroed.
 *
 * @param graph Scene graph to destroy (may be NULL).
 */
void scene_graph_destroy(scene_graph_t *graph);

/**
 * @brief Attach an entity to a parent in the scene graph.
 *
 * If the entity is already attached elsewhere, it is first detached
 * from its current parent (re-parenting).
 *
 * @param graph      Non-null scene graph.
 * @param entity_idx Entity index to attach (must be < capacity).
 * @param parent_idx Parent entity index, or SCENE_NODE_NONE for root.
 * @return 0 on success, -1 on invalid args.
 */
int scene_graph_attach(scene_graph_t *graph, uint32_t entity_idx, uint32_t parent_idx);

/**
 * @brief Detach an entity from the scene graph.
 *
 * Removes the entity from its parent's child list. Children of the
 * detached entity are reparented to SCENE_NODE_NONE (become roots).
 * The detached entity's links are reset to SCENE_NODE_NONE.
 *
 * No-op if the entity is not attached.
 *
 * @param graph      Scene graph (may be NULL — no-op).
 * @param entity_idx Entity index to detach.
 */
void scene_graph_detach(scene_graph_t *graph, uint32_t entity_idx);

/**
 * @brief Mark an entity's local transform as dirty.
 *
 * The entity will be added to the dirty list for the next
 * scene_graph_update() call.
 *
 * @param graph      Scene graph (may be NULL — no-op).
 * @param entity_idx Entity index to mark dirty.
 */
void scene_graph_mark_dirty(scene_graph_t *graph, uint32_t entity_idx);

/**
 * @brief Update world transforms for all dirty nodes.
 *
 * BFS from dirty roots, computing world = parent.world × local.
 * Dirty flag cascades to children. Static nodes are skipped.
 * After update, the dirty list is cleared.
 *
 * @param graph Scene graph (may be NULL — no-op).
 */
void scene_graph_update(scene_graph_t *graph);

#endif /* FERRUM_RENDERER_SCENE_GRAPH_H */
