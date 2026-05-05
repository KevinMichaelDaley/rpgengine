/**
 * @file npc_state_kg_init.c
 * @brief Pre-populate an NPC's personal KG from a shared source graph.
 *
 * Non-static functions (1 of 4 max):
 *   1. npc_state_kg_prepopulate
 */

#include "ferrum/npc/npc_state_manager.h"
#include <string.h>

uint32_t npc_state_kg_prepopulate(npc_state_t *npc,
                                  const npc_knowledge_graph_t *source) {
    if (!npc || !source || !source->nodes || source->node_count == 0) {
        return 0;
    }

    uint32_t copied = 0;

    for (uint32_t i = 0; i < source->node_count; i++) {
        const npc_kg_node_t *src = &source->nodes[i];
        npc_kg_node_t *dst = npc_kg_insert_node(&npc->kg, src->node_id,
                                                 src->type, src->embedding);
        if (dst) copied++;
    }

    for (uint32_t i = 0; i < source->node_count; i++) {
        const npc_kg_node_t *src = &source->nodes[i];
        for (uint32_t j = 0; j < src->edge_count; j++) {
            npc_kg_add_edge(&npc->kg, src->node_id,
                            src->edges[j].to_node_id,
                            src->edges[j].relation_id,
                            src->edges[j].weight,
                            src->edges[j].timestamp_us);
        }
    }

    npc->shared_kg = (struct npc_knowledge_graph *)source;
    return copied;
}
