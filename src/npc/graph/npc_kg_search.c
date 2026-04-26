/**
 * @file npc_kg_search.c
 * @brief Semantic search over knowledge graph via FAISS wrapper.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <string.h>

uint32_t npc_kg_search(npc_knowledge_graph_t *kg, const float *query_emb,
                       uint32_t k, uint64_t *out_ids, float *out_scores) {
    if (!kg || !query_emb || k == 0 || !out_ids || !out_scores) return 0;

    uint32_t total_found = 0;

    /* Search personal index. */
    if (kg->faiss_index) {
        int found = faiss_index_search(
            kg->faiss_index, 1, query_emb, (int)k,
            out_scores, out_ids);
        if (found > 0) {
            total_found = (uint32_t)found;
        }
    }

    /* Search shared faction index if present. */
    if (kg->shared && kg->shared->faiss_index) {
        uint64_t shared_ids[64];
        float    shared_scores[64];
        uint32_t shared_k = k < 64 ? k : 64;

        int found = faiss_index_search(
            kg->shared->faiss_index, 1, query_emb, (int)shared_k,
            shared_scores, shared_ids);

        /* Merge and deduplicate by node_id. */
        for (int i = 0; i < found && total_found < k; i++) {
            bool dup = false;
            for (uint32_t j = 0; j < total_found; j++) {
                if (out_ids[j] == shared_ids[i]) {
                    dup = true;
                    break;
                }
            }
            if (!dup) {
                out_ids[total_found] = shared_ids[i];
                out_scores[total_found] = shared_scores[i];
                total_found++;
            }
        }
    }

    return total_found;
}
