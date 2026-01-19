#include "ferrum/renderer/render_pipeline_graph.h"

#include <string.h>

static int render_pipeline_find_node(const render_pipeline_graph_t *graph,
                                     const char *name,
                                     size_t *out_index) {
    if (graph == NULL || name == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    for (size_t i = 0; i < graph->node_count; ++i) {
        const render_pipeline_graph_node_t *node = &graph->nodes[i];
        if (node->pass != NULL && node->pass->name != NULL &&
            strcmp(node->pass->name, name) == 0) {
            if (out_index != NULL) {
                *out_index = i;
            }
            return RENDER_PIPELINE_OK;
        }
    }
    return RENDER_PIPELINE_ERR_INVALID;
}

static int render_pipeline_node_is_enabled(const render_pipeline_graph_node_t *node,
                                           int depth_prepass_enabled) {
    if (node == NULL) {
        return 0;
    }
    if ((node->flags & RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS) != 0u && !depth_prepass_enabled) {
        return 0;
    }
    return 1;
}

static int render_pipeline_select_ready(const render_pipeline_graph_t *graph,
                                        const uint8_t *done,
                                        int depth_prepass_enabled,
                                        size_t *out_index) {
    for (size_t i = 0; i < graph->node_count; ++i) {
        const render_pipeline_graph_node_t *node = &graph->nodes[i];
        if (done[i]) {
            continue;
        }
        if (!render_pipeline_node_is_enabled(node, depth_prepass_enabled)) {
            continue;
        }
        int ready = 1;
        for (size_t dep = 0; dep < node->dependency_count; ++dep) {
            size_t dep_index = 0;
            if (render_pipeline_find_node(graph, node->dependencies[dep], &dep_index) !=
                RENDER_PIPELINE_OK) {
                return RENDER_PIPELINE_ERR_INVALID;
            }
            if (!done[dep_index] &&
                render_pipeline_node_is_enabled(&graph->nodes[dep_index], depth_prepass_enabled)) {
                ready = 0;
                break;
            }
        }
        if (ready) {
            *out_index = i;
            return RENDER_PIPELINE_OK;
        }
    }
    return RENDER_PIPELINE_ERR_INVALID;
}

int render_pipeline_graph_execute(const render_pipeline_graph_t *graph, int depth_prepass_enabled) {
    if (graph == NULL || graph->nodes == NULL || graph->node_count == 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    uint8_t done[32] = {0};
    size_t node_count = graph->node_count;
    if (node_count > sizeof(done)) {
        return RENDER_PIPELINE_ERR_INVALID;
    }

    size_t executed = 0;
    for (size_t pass = 0; pass < node_count; ++pass) {
        size_t index = 0;
        if (render_pipeline_select_ready(graph, done, depth_prepass_enabled, &index) !=
            RENDER_PIPELINE_OK) {
            break;
        }
        const render_pipeline_graph_node_t *node = &graph->nodes[index];
        if (node->pass == NULL) {
            return RENDER_PIPELINE_ERR_INVALID;
        }
        if (node->pass->begin != NULL) {
            node->pass->begin(node->pass->user_data);
        }
        if (node->pass->submit != NULL) {
            node->pass->submit(node->pass->user_data);
        }
        if (node->pass->end != NULL) {
            node->pass->end(node->pass->user_data);
        }
        done[index] = 1u;
        executed++;
    }

    for (size_t i = 0; i < graph->node_count; ++i) {
        if (render_pipeline_node_is_enabled(&graph->nodes[i], depth_prepass_enabled) && !done[i]) {
            return RENDER_PIPELINE_ERR_INVALID;
        }
    }
    if (executed == 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    return RENDER_PIPELINE_OK;
}
