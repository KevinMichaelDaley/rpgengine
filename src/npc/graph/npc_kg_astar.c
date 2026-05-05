/**
 * @file npc_kg_astar.c
 * @brief A* pathfinding on the knowledge graph.
 *
 * Uses Dijkstra (heuristic = 0) to find the path with lowest total weight.
 * Respects allowed_relations whitelist and max_cost cutoff.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    float    g_cost;
    uint32_t node_index;
} pq_entry_t;

typedef struct {
    pq_entry_t *heap;
    uint32_t   count;
    uint32_t   capacity;
} pq_t;

/* ------------------------------------------------------------------ */
/* Priority queue helpers                                             */
/* ------------------------------------------------------------------ */

static bool pq_push(pq_t *pq, float g_cost, uint32_t node_index) {
    if (pq->count >= pq->capacity) return false;
    uint32_t i = pq->count++;
    while (i > 0) {
        uint32_t parent = (i - 1) / 2;
        if (pq->heap[parent].g_cost <= g_cost) break;
        pq->heap[i] = pq->heap[parent];
        i = parent;
    }
    pq->heap[i].g_cost = g_cost;
    pq->heap[i].node_index = node_index;
    return true;
}

static int pq_pop(pq_t *pq, float *out_cost, uint32_t *out_index) {
    if (pq->count == 0) return 0;
    *out_cost = pq->heap[0].g_cost;
    *out_index = pq->heap[0].node_index;
    pq->count--;
    if (pq->count == 0) return 1;

    pq_entry_t last = pq->heap[pq->count];
    uint32_t i = 0;
    for (;;) {
        uint32_t left = 2 * i + 1;
        uint32_t right = 2 * i + 2;
        uint32_t smallest = i;
        if (left < pq->count && pq->heap[left].g_cost < pq->heap[smallest].g_cost)
            smallest = left;
        if (right < pq->count && pq->heap[right].g_cost < pq->heap[smallest].g_cost)
            smallest = right;
        if (smallest == i) break;
        pq->heap[i] = pq->heap[smallest];
        i = smallest;
    }
    pq->heap[i] = last;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Check if relation is allowed                                       */
/* ------------------------------------------------------------------ */

static bool relation_allowed(uint32_t rel_id,
                              const uint32_t *allowed,
                              uint32_t allowed_count) {
    if (allowed_count == 0) return true;
    for (uint32_t i = 0; i < allowed_count; i++) {
        if (allowed[i] == rel_id) return true;
    }
    return false;
}

/* ------------------------------------------------------------------ */
/* Reconstruct path                                                   */
/* ------------------------------------------------------------------ */

static void reconstruct_path(const npc_knowledge_graph_t *kg,
                              const uint32_t *parent,
                              const uint32_t *edge_idx,
                              uint32_t goal_idx,
                              npc_kg_path_result_t *result) {
    uint32_t max_len = kg->node_count;
    uint32_t *nodes = (uint32_t *)malloc(max_len * sizeof(uint32_t));
    uint32_t *rels = (uint32_t *)malloc(max_len * sizeof(uint32_t));
    if (!nodes || !rels) {
        free(nodes);
        free(rels);
        return;
    }
    uint32_t len = 0;

    uint32_t cur = goal_idx;
    while (cur != (uint32_t)-1 && len < max_len) {
        nodes[len] = cur;
        if (parent[cur] != (uint32_t)-1) {
            uint32_t p = parent[cur];
            rels[len] = kg->nodes[p].edges[edge_idx[cur]].relation_id;
        }
        len++;
        cur = (parent[cur] != (uint32_t)-1) ? parent[cur] : (uint32_t)-1;
    }

    result->step_count = len - 1;
    if (len == 0) {
        free(nodes);
        free(rels);
        return;
    }

    result->node_ids = (uint64_t *)malloc((size_t)len * sizeof(uint64_t));
    if (len > 1) {
        result->relation_ids = (uint32_t *)malloc((size_t)(len - 1) * sizeof(uint32_t));
    }

    if (!result->node_ids || (len > 1 && !result->relation_ids)) {
        free(result->node_ids);
        free(result->relation_ids);
        result->node_ids = NULL;
        result->relation_ids = NULL;
        result->step_count = 0;
    } else {
        for (uint32_t i = 0; i < len; i++) {
            result->node_ids[i] = kg->nodes[nodes[len - 1 - i]].node_id;
        }
        for (uint32_t i = 0; i < len - 1; i++) {
            result->relation_ids[i] = rels[len - 2 - i];
        }
    }

    free(nodes);
    free(rels);
}

/* ================================================================== */
/* Public API                                                         */
/* ================================================================== */

bool npc_kg_astar(const npc_knowledge_graph_t *kg,
                  const npc_kg_path_request_t *req,
                  npc_kg_path_result_t *result) {
    memset(result, 0, sizeof(*result));
    if (!kg || !req || !result) return false;
    if (kg->node_count == 0) return false;

    /* Find start and goal node indices. */
    uint32_t start_idx = (uint32_t)-1;
    uint32_t goal_idx = (uint32_t)-1;
    for (uint32_t i = 0; i < kg->node_count; i++) {
        if (kg->nodes[i].node_id == req->start_node_id) start_idx = i;
        if (kg->nodes[i].node_id == req->goal_node_id) goal_idx = i;
    }
    if (start_idx == (uint32_t)-1 || goal_idx == (uint32_t)-1) return false;

    if (start_idx == goal_idx) {
        result->found = true;
        result->step_count = 0;
        result->node_ids = (uint64_t *)malloc(sizeof(uint64_t));
        if (result->node_ids) {
            result->node_ids[0] = kg->nodes[start_idx].node_id;
        }
        result->total_cost = 0.0f;
        return true;
    }

    float *g_score = (float *)malloc(kg->node_count * sizeof(float));
    uint32_t *parent = (uint32_t *)malloc(kg->node_count * sizeof(uint32_t));
    uint32_t *edge_idx = (uint32_t *)malloc(kg->node_count * sizeof(uint32_t));
    bool *visited = (bool *)calloc(kg->node_count, sizeof(bool));

    if (!g_score || !parent || !edge_idx || !visited) {
        free(g_score); free(parent); free(edge_idx); free(visited);
        return false;
    }

    for (uint32_t i = 0; i < kg->node_count; i++) {
        g_score[i] = 1e30f;
        parent[i] = (uint32_t)-1;
        edge_idx[i] = (uint32_t)-1;
    }
    g_score[start_idx] = 0.0f;

    uint32_t pq_cap = kg->node_count * 2;
    pq_t pq;
    pq.heap = (pq_entry_t *)malloc(pq_cap * sizeof(pq_entry_t));
    if (!pq.heap) {
        free(g_score); free(parent); free(edge_idx); free(visited);
        return false;
    }
    pq.count = 0;
    pq.capacity = pq_cap;

    if (!pq_push(&pq, 0.0f, start_idx)) {
        free(pq.heap);
        free(g_score); free(parent); free(edge_idx); free(visited);
        return false;
    }

    bool found = false;

    while (pq.count > 0) {
        float cur_cost;
        uint32_t cur;
        if (!pq_pop(&pq, &cur_cost, &cur)) break;
        if (visited[cur]) continue;
        visited[cur] = true;

        if (cur == goal_idx) break;

        npc_kg_node_t *node = &kg->nodes[cur];
        for (uint32_t j = 0; j < node->edge_count; j++) {
            npc_kg_edge_t *e = &node->edges[j];

            if (!relation_allowed(e->relation_id,
                                  req->allowed_relations,
                                  req->allowed_relation_count))
                continue;

            if (e->weight <= 0.0f) continue;

            /* Find target node index. */
            uint32_t nxt_idx = (uint32_t)-1;
            for (uint32_t k = 0; k < kg->node_count; k++) {
                if (kg->nodes[k].node_id == e->to_node_id) {
                    nxt_idx = k;
                    break;
                }
            }
            if (nxt_idx == (uint32_t)-1) continue;

            float new_cost = cur_cost + e->weight;
            if (new_cost > req->max_cost) continue;
            if (new_cost < g_score[nxt_idx]) {
                g_score[nxt_idx] = new_cost;
                parent[nxt_idx] = cur;
                edge_idx[nxt_idx] = j;
                if (!pq_push(&pq, new_cost, nxt_idx)) goto cleanup;
            }
        }
    }

    if (visited[goal_idx] && g_score[goal_idx] <= req->max_cost) {
        reconstruct_path(kg, parent, edge_idx, goal_idx, result);
        result->total_cost = g_score[goal_idx];
        result->found = true;
        found = true;
    }

cleanup:
    free(pq.heap);
    free(g_score);
    free(parent);
    free(edge_idx);
    free(visited);
    return found;
}
