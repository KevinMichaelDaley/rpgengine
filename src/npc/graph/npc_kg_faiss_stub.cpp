/**
 * @file npc_kg_faiss_stub.cpp
 * @brief Stub FAISS wrapper — provides no-op implementations for headless builds.
 */
#include <cstdint>

extern "C" {

void *faiss_index_create(int dim) {
    (void)dim;
    return nullptr;
}

void faiss_index_add(void *index, int n, const float *vectors,
                     const uint64_t *ids) {
    (void)index; (void)n; (void)vectors; (void)ids;
}

void faiss_index_search(void *index, int n, const float *queries,
                        int k, float *distances, uint64_t *labels) {
    (void)index; (void)n; (void)queries; (void)k;
    for (int i = 0; i < n * k; i++) {
        if (distances) distances[i] = 1e9f;
        if (labels) labels[i] = 0;
    }
}

void faiss_index_destroy(void *index) {
    (void)index;
}

void faiss_index_free(void *index) {
    (void)index;
}

int faiss_index_count(void *index) {
    (void)index;
    return 0;
}

void faiss_index_reset(void *index) {
    (void)index;
}

} /* extern "C" */
