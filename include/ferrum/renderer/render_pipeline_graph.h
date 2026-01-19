#ifndef FERRUM_RENDERER_RENDER_PIPELINE_GRAPH_H
#define FERRUM_RENDERER_RENDER_PIPELINE_GRAPH_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/render_pipeline.h"

/** @file
 * @brief Render pipeline dependency graph representation.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Graph node flag indicating depth pre-pass. */
#define RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS 1u

/** Render pipeline graph node. */
typedef struct render_pipeline_graph_node {
    render_pass_t *pass;
    const char **dependencies;
    size_t dependency_count;
    uint32_t flags;
} render_pipeline_graph_node_t;

/** Render pipeline graph. */
typedef struct render_pipeline_graph {
    render_pipeline_graph_node_t *nodes;
    size_t node_count;
} render_pipeline_graph_t;

/**
 * @brief Execute render pipeline graph.
 * @param graph Graph to execute.
 * @param depth_prepass_enabled Non-zero to run depth pre-pass nodes.
 * @return Status code.
 */
int render_pipeline_graph_execute(const render_pipeline_graph_t *graph, int depth_prepass_enabled);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_PIPELINE_GRAPH_H */
