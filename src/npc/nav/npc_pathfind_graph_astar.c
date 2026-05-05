/**
 * @file npc_pathfind_graph_astar.c
 * @brief A* on the chunk navigation graph.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_graph_astar
 */

#include "ferrum/npc/npc_pathfind.h"
#include "ferrum/npc/npc_nav_graph.h"
#include "ferrum/physics/phys_vec3_ops.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ── Internal min-heap for graph nodes ───────────────────────────── */

#define GHEAP_MAX 1024

typedef struct {
    float    f;
    uint32_t node;
} gheap_entry_t;

typedef struct {
    gheap_entry_t entries[GHEAP_MAX];
    uint32_t      size;
} gheap_t;

static void gheap_init(gheap_t *h) { h->size = 0; }

static void gheap_push(gheap_t *h, float f, uint32_t node) {
    if (h->size >= GHEAP_MAX) return;
    uint32_t i = h->size++;
    while (i > 0) {
        uint32_t p = (i - 1) / 2;
        if (h->entries[p].f <= f) break;
        h->entries[i] = h->entries[p];
        i = p;
    }
    h->entries[i].f = f;
    h->entries[i].node = node;
}

static gheap_entry_t gheap_pop(gheap_t *h) {
    gheap_entry_t top = h->entries[0];
    if (h->size <= 1) { h->size = 0; return top; }
    gheap_entry_t last = h->entries[--h->size];
    uint32_t i = 0;
    for (;;) {
        uint32_t left = 2 * i + 1, right = 2 * i + 2, smallest = i;
        if (left < h->size && h->entries[left].f < h->entries[smallest].f)
            smallest = left;
        if (right < h->size && h->entries[right].f < h->entries[smallest].f)
            smallest = right;
        if (smallest == i) break;
        h->entries[i] = h->entries[smallest];
        i = smallest;
    }
    h->entries[i] = last;
    return top;
}

/* ── Public: graph A* ────────────────────────────────────────────── */

bool npc_graph_astar(const npc_nav_graph_t *graph,
                     uint32_t start_node,
                     uint32_t goal_node,
                     uint32_t *out_nodes,
                     uint32_t *out_count,
                     uint32_t max_nodes) {
    if (!graph || !out_nodes || !out_count || max_nodes == 0) return false;
    if (start_node >= graph->node_count || goal_node >= graph->node_count)
        return false;

    uint32_t n = graph->node_count;
    float *g = (float *)malloc(n * sizeof(float));
    uint32_t *parent = (uint32_t *)malloc(n * sizeof(uint32_t));
    if (!g || !parent) { free(g); free(parent); return false; }
    for (uint32_t i = 0; i < n; i++) g[i] = INFINITY;
    memset(parent, 0xFF, n * sizeof(uint32_t));

    gheap_t heap;
    gheap_init(&heap);

    phys_vec3_t goal_c = graph->nodes[goal_node].centroid;
    float h0 = phys_vec3_distance(graph->nodes[start_node].centroid, goal_c);
    g[start_node] = 0.0f;
    gheap_push(&heap, h0, start_node);
    bool reached = false;

    while (heap.size > 0) {
        gheap_entry_t cur = gheap_pop(&heap);
        if (cur.node == goal_node) { reached = true; break; }

        const npc_nav_graph_node_t *nd = &graph->nodes[cur.node];
        for (uint32_t e = 0; e < nd->edge_count; e++) {
            uint32_t nb = nd->edges[e].to_node_id;
            if (nb >= n) continue;
            float new_g = g[cur.node] + nd->edges[e].cost;
            if (new_g < g[nb]) {
                g[nb] = new_g;
                parent[nb] = cur.node;
                float h = phys_vec3_distance(graph->nodes[nb].centroid, goal_c);
                gheap_push(&heap, new_g + h, nb);
            }
        }
    }

    if (!reached) { free(g); free(parent); return false; }

    /* Reconstruct path. */
    uint32_t path[256];
    uint32_t plen = 0;
    uint32_t ci = goal_node;
    while (ci != UINT32_MAX && plen < 256) {
        path[plen++] = ci;
        if (ci == start_node) break;
        ci = parent[ci];
    }

    *out_count = 0;
    for (int32_t i = (int32_t)plen - 1; i >= 0 && *out_count < max_nodes; i--) {
        out_nodes[(*out_count)++] = path[(uint32_t)i];
    }

    free(g);
    free(parent);
    return true;
}
