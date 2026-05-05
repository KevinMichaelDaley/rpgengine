/**
 * @file npc_kg_reachability.c
 * @brief Nav result -> KG edge sync (reachability).
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <stdlib.h>
#include <string.h>

static npc_kg_node_t *find_node(npc_knowledge_graph_t *kg, uint64_t node_id) {
    for (uint32_t i = 0; i < kg->node_count; i++) {
        if (kg->nodes[i].node_id == node_id) return &kg->nodes[i];
    }
    return NULL;
}

static npc_kg_edge_t *find_edge_by_rel(npc_kg_node_t *from, uint64_t to_id,
                                       uint32_t rel_id) {
    for (uint32_t i = 0; i < from->edge_count; i++) {
        npc_kg_edge_t *e = &from->edges[i];
        if (e->to_node_id == to_id && e->relation_id == rel_id) return e;
    }
    return NULL;
}

void npc_kg_set_reachable(npc_knowledge_graph_t *kg,
                          uint64_t from_entity,
                          uint64_t to_entity,
                          float path_cost,
                          const vec3_t *waypoints,
                          uint32_t waypoint_count) {
    if (!kg) return;

    npc_kg_node_t *from = find_node(kg, from_entity);
    if (!from) return;

    uint32_t reachable_rel = npc_kg_relation_id("reachable_from");

    npc_kg_edge_t *existing = find_edge_by_rel(from, to_entity, reachable_rel);
    if (existing) {
        existing->weight = path_cost;
        existing->timestamp_us = 0;
        if (path_cost >= 0.0f) {
            existing->flags |= NPC_KG_EDGE_REACHABLE;
            existing->flags &= ~NPC_KG_EDGE_STALE;
        } else {
            existing->flags |= NPC_KG_EDGE_STALE;
            existing->flags &= ~NPC_KG_EDGE_REACHABLE;
        }
        return;
    }

    if (from->edge_count >= from->edge_cap) {
        uint32_t new_cap = from->edge_cap == 0 ? 4 : from->edge_cap * 2;
        npc_kg_edge_t *new_edges = (npc_kg_edge_t *)realloc(
            from->edges, new_cap * sizeof(npc_kg_edge_t));
        if (!new_edges) return;
        from->edges = new_edges;
        from->edge_cap = new_cap;
    }

    npc_kg_edge_t *e = &from->edges[from->edge_count++];
    memset(e, 0, sizeof(*e));
    e->to_node_id = to_entity;
    e->relation_id = reachable_rel;
    e->weight = path_cost;
    e->timestamp_us = 0;
    if (path_cost >= 0.0f) {
        e->flags = NPC_KG_EDGE_REACHABLE;
    } else {
        e->flags = NPC_KG_EDGE_STALE;
    }

    (void)waypoints;
    (void)waypoint_count;
}
