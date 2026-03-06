/**
 * @file scene_graph_update.c
 * @brief Dirty marking and BFS world transform propagation.
 *
 * Processes the dirty list, propagating world = parent.world × local
 * downward through children. Static nodes are skipped. After update,
 * the dirty list is cleared and all dirty flags are reset.
 */

#include "ferrum/renderer/scene/scene_graph.h"
#include "ferrum/math/mat4.h"

void scene_graph_mark_dirty(scene_graph_t *graph, uint32_t entity_idx) {
    if (!graph) return;
    if (entity_idx >= graph->capacity) return;

    scene_node_t *node = &graph->nodes[entity_idx];
    if (node->flags & (SCENE_NODE_DIRTY_LOCAL | SCENE_NODE_DIRTY_WORLD)) {
        return; /* Already in the dirty list. */
    }

    node->flags |= SCENE_NODE_DIRTY_LOCAL;

    /* Add to dirty list (bounds check). */
    if (graph->dirty_count < graph->capacity) {
        graph->dirty_list[graph->dirty_count++] = entity_idx;
    }
}

/**
 * @brief Recursively propagate world transforms from parent to children.
 *
 * @param graph      Scene graph.
 * @param node_idx   Node to update.
 * @param parent_world Parent's world transform (identity for roots).
 */
static void propagate_(scene_graph_t *graph, uint32_t node_idx,
                       const mat4_t *parent_world) {
    scene_node_t *node = &graph->nodes[node_idx];

    /* Skip static nodes. */
    if (node->flags & SCENE_NODE_STATIC) return;

    /* Compute world transform. */
    node->world_transform = mat4_mul(*parent_world, node->local_transform);
    node->flags &= ~(SCENE_NODE_DIRTY_LOCAL | SCENE_NODE_DIRTY_WORLD);

    /* Cascade to children. */
    uint32_t child = node->first_child;
    while (child != SCENE_NODE_NONE) {
        propagate_(graph, child, &node->world_transform);
        child = graph->nodes[child].next_sibling;
    }
}

void scene_graph_update(scene_graph_t *graph) {
    if (!graph) return;

    for (uint32_t i = 0; i < graph->dirty_count; i++) {
        uint32_t idx = graph->dirty_list[i];
        if (idx >= graph->capacity) continue;

        scene_node_t *node = &graph->nodes[idx];
        if (!(node->flags & (SCENE_NODE_DIRTY_LOCAL | SCENE_NODE_DIRTY_WORLD))) {
            continue; /* Already processed (a parent cascaded through it). */
        }

        /* Find the parent's world transform. */
        mat4_t parent_world;
        if (node->parent != SCENE_NODE_NONE) {
            parent_world = graph->nodes[node->parent].world_transform;
        } else {
            parent_world = mat4_identity();
        }

        propagate_(graph, idx, &parent_world);
    }

    graph->dirty_count = 0;
}
