/**
 * @file npc_kg_faiss_wrapper.cpp
 * @brief FAISS C++ wrapper with C-compatible interface.
 *
 * Uses faiss::IndexIDMap over faiss::IndexFlatIP so that custom
 * 64-bit IDs are preserved during add/search.
 */

/* Include project header first (opens/closes extern "C") */
#include "ferrum/npc/npc_knowledge_graph.h"

/* C++ / FAISS headers after the extern "C" block is closed */
#include <cstdint>
#include <vector>

#include <faiss/IndexFlat.h>
#include <faiss/IndexIDMap.h>

extern "C" {

void *faiss_index_create(int dim) {
    if (dim <= 0) return nullptr;
    faiss::IndexFlatIP *flat = new faiss::IndexFlatIP(dim);
    return new faiss::IndexIDMap(flat);
}

void faiss_index_add(void *index, int n, const float *vectors,
                     const uint64_t *ids) {
    if (!index || n <= 0 || !vectors || !ids) return;
    faiss::IndexIDMap *idx = static_cast<faiss::IndexIDMap *>(index);
    std::vector<faiss::idx_t> labels(n);
    for (int i = 0; i < n; i++) {
        labels[i] = static_cast<faiss::idx_t>(ids[i]);
    }
    idx->add_with_ids(n, vectors, labels.data());
}

int faiss_index_search(void *index, int nq, const float *queries,
                       int k, float *distances, uint64_t *ids) {
    if (!index || nq <= 0 || !queries || k <= 0 || !distances || !ids)
        return 0;
    faiss::IndexIDMap *idx = static_cast<faiss::IndexIDMap *>(index);
    if (idx->ntotal == 0) return 0;

    std::vector<faiss::idx_t> labels((size_t)nq * k);
    idx->search(nq, queries, k, distances, labels.data());

    for (int i = 0; i < nq * k; i++) {
        ids[i] = (labels[i] < 0) ? (uint64_t)-1
                                 : static_cast<uint64_t>(labels[i]);
    }
    return k;
}

void faiss_index_destroy(void *index) {
    delete static_cast<faiss::IndexIDMap *>(index);
}

} /* extern "C" */
