/**
 * @file scene_graph_init.c
 * @brief Scene graph initialization and destruction.
 *
 * Allocates the parallel node array and dirty list. All nodes start
 * unattached (links = SCENE_NODE_NONE, transforms = identity).
 */

#include "ferrum/renderer/scene/scene_graph.h"
#include "ferrum/math/mat4.h"
#include <stdlib.h>
#include <string.h>

int scene_graph_init(scene_graph_t *graph, uint32_t capacity) {
    if (!graph || capacity == 0) return -1;

    graph->nodes = malloc(capacity * sizeof(scene_node_t));
    if (!graph->nodes) return -1;

    graph->dirty_list = malloc(capacity * sizeof(uint32_t));
    if (!graph->dirty_list) {
        free(graph->nodes);
        graph->nodes = NULL;
        return -1;
    }

    graph->capacity    = capacity;
    graph->dirty_count = 0;

    /* Initialize all nodes to unattached with identity transforms. */
    mat4_t ident = mat4_identity();
    for (uint32_t i = 0; i < capacity; i++) {
        graph->nodes[i].parent          = SCENE_NODE_NONE;
        graph->nodes[i].first_child     = SCENE_NODE_NONE;
        graph->nodes[i].next_sibling    = SCENE_NODE_NONE;
        graph->nodes[i].flags           = 0;
        graph->nodes[i].local_transform = ident;
        graph->nodes[i].world_transform = ident;
    }

    return 0;
}

void scene_graph_destroy(scene_graph_t *graph) {
    if (!graph) return;
    free(graph->nodes);
    free(graph->dirty_list);
    memset(graph, 0, sizeof(*graph));
}
