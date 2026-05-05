/**
 * @file npc_kg_spatial.c
 * @brief Spatial edge helpers: upsert and decay.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static npc_kg_node_t *find_node_by_id(npc_knowledge_graph_t *kg,
                                      uint64_t node_id) {
    for (uint32_t i = 0; i < kg->node_count; i++) {
        if (kg->nodes[i].node_id == node_id) return &kg->nodes[i];
    }
    return NULL;
}

static npc_kg_edge_t *find_edge(npc_kg_node_t *from, uint64_t to_id,
                                uint32_t rel_id) {
    for (uint32_t i = 0; i < from->edge_count; i++) {
        npc_kg_edge_t *e = &from->edges[i];
        if (e->to_node_id == to_id && e->relation_id == rel_id) return e;
    }
    return NULL;
}

bool npc_kg_upsert_spatial_edge(npc_knowledge_graph_t *kg,
                                uint64_t from_entity,
                                uint64_t to_entity,
                                uint32_t relation_id,
                                float weight,
                                uint64_t timestamp_us) {
    if (!kg) return false;

    npc_kg_node_t *from = find_node_by_id(kg, from_entity);
    if (!from) return false;

    npc_kg_edge_t *existing = find_edge(from, to_entity, relation_id);
    if (existing) {
        existing->weight = weight;
        existing->timestamp_us = timestamp_us;
        existing->flags |= NPC_KG_EDGE_SPATIAL;
        return true;
    }

    if (from->edge_count >= from->edge_cap) {
        uint32_t new_cap = from->edge_cap == 0 ? 4 : from->edge_cap * 2;
        npc_kg_edge_t *new_edges = (npc_kg_edge_t *)realloc(
            from->edges, new_cap * sizeof(npc_kg_edge_t));
        if (!new_edges) return false;
        from->edges = new_edges;
        from->edge_cap = new_cap;
    }

    npc_kg_edge_t *e = &from->edges[from->edge_count++];
    memset(e, 0, sizeof(*e));
    e->to_node_id = to_entity;
    e->relation_id = relation_id;
    e->weight = weight;
    e->timestamp_us = timestamp_us;
    e->flags = NPC_KG_EDGE_SPATIAL;
    return true;
}

void npc_kg_spatial_decay(npc_knowledge_graph_t *kg, float dt_seconds,
                          float lambda) {
    if (!kg || dt_seconds <= 0.0f || lambda <= 0.0f) return;

    float factor = expf(-lambda * dt_seconds);

    for (uint32_t i = 0; i < kg->node_count; i++) {
        npc_kg_node_t *n = &kg->nodes[i];
        for (uint32_t j = 0; j < n->edge_count; j++) {
            npc_kg_edge_t *e = &n->edges[j];
            if (!(e->flags & NPC_KG_EDGE_SPATIAL)) continue;
            e->weight *= factor;
            if (e->weight < 0.001f) {
                e->weight = 0.0f;
            }
        }
    }
}
