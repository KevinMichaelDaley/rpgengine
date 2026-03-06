/**
 * @file scene_graph_attach.c
 * @brief Attach and re-parent entities in the LCRS scene graph.
 */

#include "ferrum/renderer/scene/scene_graph.h"

/* ── Internal helper: remove entity from its current parent's child list ── */

/**
 * @brief Remove entity_idx from its parent's sibling chain.
 *
 * Does nothing if the entity has no parent (parent == SCENE_NODE_NONE).
 */
static void unlink_from_parent_(scene_graph_t *graph, uint32_t entity_idx) {
    scene_node_t *node = &graph->nodes[entity_idx];
    uint32_t parent = node->parent;

    if (parent == SCENE_NODE_NONE) {
        /* Root node or unattached — nothing to unlink from. */
        return;
    }

    scene_node_t *pnode = &graph->nodes[parent];

    if (pnode->first_child == entity_idx) {
        /* Entity is the first child — advance first_child to next sibling. */
        pnode->first_child = node->next_sibling;
    } else {
        /* Walk sibling chain to find predecessor. */
        uint32_t prev = pnode->first_child;
        while (prev != SCENE_NODE_NONE && graph->nodes[prev].next_sibling != entity_idx) {
            prev = graph->nodes[prev].next_sibling;
        }
        if (prev != SCENE_NODE_NONE) {
            graph->nodes[prev].next_sibling = node->next_sibling;
        }
    }

    node->next_sibling = SCENE_NODE_NONE;
    node->parent = SCENE_NODE_NONE;
}

int scene_graph_attach(scene_graph_t *graph, uint32_t entity_idx, uint32_t parent_idx) {
    if (!graph) return -1;
    if (entity_idx >= graph->capacity) return -1;
    if (parent_idx != SCENE_NODE_NONE && parent_idx >= graph->capacity) return -1;

    /* If already attached somewhere, detach first (re-parent). */
    scene_node_t *node = &graph->nodes[entity_idx];
    if (node->parent != SCENE_NODE_NONE || node->first_child != SCENE_NODE_NONE ||
        node->next_sibling != SCENE_NODE_NONE) {
        /* Check if node was previously attached (parent set or was a root with children). */
        unlink_from_parent_(graph, entity_idx);
    }

    node->parent = parent_idx;

    if (parent_idx != SCENE_NODE_NONE) {
        /* Insert as first child of parent (prepend to sibling chain). */
        scene_node_t *pnode = &graph->nodes[parent_idx];
        node->next_sibling = pnode->first_child;
        pnode->first_child = entity_idx;
    }

    return 0;
}
