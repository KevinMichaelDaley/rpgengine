/**
 * @file scene_graph_detach.c
 * @brief Detach entities from the LCRS scene graph.
 *
 * Children of the detached entity are reparented to SCENE_NODE_NONE
 * (become root nodes).
 */

#include "ferrum/renderer/scene/scene_graph.h"

void scene_graph_detach(scene_graph_t *graph, uint32_t entity_idx) {
    if (!graph) return;
    if (entity_idx >= graph->capacity) return;

    scene_node_t *node = &graph->nodes[entity_idx];

    /* If the node has a parent, unlink from parent's child list. */
    uint32_t parent = node->parent;
    if (parent != SCENE_NODE_NONE) {
        scene_node_t *pnode = &graph->nodes[parent];

        if (pnode->first_child == entity_idx) {
            pnode->first_child = node->next_sibling;
        } else {
            uint32_t prev = pnode->first_child;
            while (prev != SCENE_NODE_NONE &&
                   graph->nodes[prev].next_sibling != entity_idx) {
                prev = graph->nodes[prev].next_sibling;
            }
            if (prev != SCENE_NODE_NONE) {
                graph->nodes[prev].next_sibling = node->next_sibling;
            }
        }
    }

    /* Reparent children to SCENE_NODE_NONE (they become roots). */
    uint32_t child = node->first_child;
    while (child != SCENE_NODE_NONE) {
        uint32_t next = graph->nodes[child].next_sibling;
        graph->nodes[child].parent       = SCENE_NODE_NONE;
        graph->nodes[child].next_sibling = SCENE_NODE_NONE;
        child = next;
    }

    /* Reset the detached node. */
    node->parent       = SCENE_NODE_NONE;
    node->first_child  = SCENE_NODE_NONE;
    node->next_sibling = SCENE_NODE_NONE;
}
