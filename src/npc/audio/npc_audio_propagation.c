/**
 * @file npc_audio_propagation.c
 * @brief Audio propagation graph: distance-based attenuation stub.
 *
 * Non-static functions (3 of 4 max):
 *   1. npc_audio_graph_init
 *   2. npc_audio_graph_destroy
 *   3. npc_audio_graph_query
 */

#include "ferrum/npc/npc_audio_propagation.h"
#include <stdlib.h>
#include <string.h>

/* ── Lifecycle ──────────────────────────────────────────────────── */

void npc_audio_graph_init(npc_audio_graph_t *graph) {
    if (!graph) return;
    memset(graph, 0, sizeof(*graph));
    graph->medium = (npc_acoustic_material_t)NPC_ACOUSTIC_MATERIAL_AIR;
}

void npc_audio_graph_destroy(npc_audio_graph_t *graph) {
    if (!graph) return;
    for (uint32_t i = 0; i < graph->node_count; i++) {
        free(graph->nodes[i].edges);
    }
    free(graph->nodes);
    memset(graph, 0, sizeof(*graph));
}

/* ── Query ──────────────────────────────────────────────────────── */

float npc_audio_graph_query(const npc_audio_graph_t *graph,
                            vec3_t a, vec3_t b) {
    float dist = vec3_distance(a, b);
    float atten;

    if (dist < 1e-4f) {
        atten = 0.0f;
    } else {
        atten = 20.0f * log10f(fmaxf(dist, 1.0f));
    }

    if (graph) {
        atten += graph->medium.absorption_mid;
    }

    if (atten > NPC_AUDIO_INAUDIBLE_THRESHOLD) {
        return NPC_AUDIO_INAUDIBLE_THRESHOLD;
    }
    return atten;
}
