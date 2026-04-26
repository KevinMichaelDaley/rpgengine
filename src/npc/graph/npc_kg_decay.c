/**
 * @file npc_kg_decay.c
 * @brief Edge weight decay over simulated time.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <math.h>

void npc_kg_decay_edges(npc_knowledge_graph_t *kg, float dt_seconds,
                        float lambda) {
    if (!kg || dt_seconds <= 0.0f || lambda <= 0.0f) return;

    float factor = expf(-lambda * dt_seconds);

    for (uint32_t i = 0; i < kg->node_count; i++) {
        npc_kg_node_t *n = &kg->nodes[i];
        for (uint32_t j = 0; j < n->edge_count; j++) {
            npc_kg_edge_t *e = &n->edges[j];
            e->weight *= factor;
            if (e->weight < 0.001f) {
                e->weight = 0.0f;
            }
        }
    }
}
