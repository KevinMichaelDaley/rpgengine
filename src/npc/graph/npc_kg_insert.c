/**
 * @file npc_kg_insert.c
 * @brief Knowledge graph node/edge insertion.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Node insert                                                        */
/* ------------------------------------------------------------------ */

npc_kg_node_t *npc_kg_insert_node(npc_knowledge_graph_t *kg, uint64_t node_id,
                                  uint32_t type, const float *embedding) {
    if (!kg || kg->node_count >= kg->node_cap) return NULL;

    npc_kg_node_t *n = &kg->nodes[kg->node_count++];
    n->node_id = node_id;
    n->type = type;
    n->edge_count = 0;
    n->edge_cap = 0;
    n->edges = NULL;

    if (embedding && kg->embedding_dim > 0) {
        size_t emb_size = kg->embedding_dim * sizeof(float);
        n->embedding = (float *)malloc(emb_size);
        if (n->embedding) {
            memcpy(n->embedding, embedding, emb_size);
        }
    } else {
        n->embedding = NULL;
    }

    return n;
}

/* ------------------------------------------------------------------ */
/* Edge add                                                           */
/* ------------------------------------------------------------------ */

bool npc_kg_add_edge(npc_knowledge_graph_t *kg, uint64_t from_id,
                     uint64_t to_id, uint32_t relation_id,
                     float weight, uint64_t timestamp_us) {
    if (!kg) return false;

    npc_kg_node_t *from = NULL;
    for (uint32_t i = 0; i < kg->node_count; i++) {
        if (kg->nodes[i].node_id == from_id) {
            from = &kg->nodes[i];
            break;
        }
    }
    if (!from) return false;

    if (from->edge_count >= from->edge_cap) {
        uint32_t new_cap = from->edge_cap == 0 ? 4 : from->edge_cap * 2;
        npc_kg_edge_t *new_edges = (npc_kg_edge_t *)realloc(
            from->edges, new_cap * sizeof(npc_kg_edge_t));
        if (!new_edges) return false;
        from->edges = new_edges;
        from->edge_cap = new_cap;
    }

    npc_kg_edge_t *e = &from->edges[from->edge_count++];
    e->to_node_id = to_id;
    e->relation_id = relation_id;
    e->weight = weight;
    e->timestamp_us = timestamp_us;
    return true;
}
