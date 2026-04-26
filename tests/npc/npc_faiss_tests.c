/**
 * @file npc_faiss_tests.c
 * @brief FAISS wrapper tests: create/add/search/destroy.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

#define RUN(fn) do { printf("  %-48s ", #fn); fn(); } while (0)
#define ASSERT_TRUE(expr) do { \
    if (!(expr)) { \
        printf("FAIL (%s:%d)\n", __FILE__, __LINE__); \
        g_fail++; \
        return; \
    } \
} while (0)
#define ASSERT_FLOAT_NEAR(exp, act, tol) do { \
    if (fabsf((exp) - (act)) > (tol)) { \
        printf("FAIL (%s:%d) expected %.6f got %.6f\n", \
               __FILE__, __LINE__, (float)(exp), (float)(act)); \
        g_fail++; \
        return; \
    } \
} while (0)
#define PASS() do { printf("PASS\n"); g_pass++; } while (0)

static int g_pass = 0;
static int g_fail = 0;

static void test_faiss_create_destroy(void) {
    void *idx = faiss_index_create(8);
    ASSERT_TRUE(idx != NULL);
    faiss_index_destroy(idx);
    PASS();
}

static void test_faiss_add_search(void) {
    void *idx = faiss_index_create(4);
    ASSERT_TRUE(idx != NULL);

    float vecs[12] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
    };
    uint64_t ids[3] = {100, 200, 300};
    faiss_index_add(idx, 3, vecs, ids);

    float query[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float dists[2];
    uint64_t out_ids[2];
    int found = faiss_index_search(idx, 1, query, 2, dists, out_ids);
    ASSERT_TRUE(found >= 1);
    ASSERT_TRUE(out_ids[0] == 100);
    ASSERT_FLOAT_NEAR(1.0f, dists[0], 0.01f);

    faiss_index_destroy(idx);
    PASS();
}

static void test_faiss_cosine_similarity(void) {
    void *idx = faiss_index_create(2);
    ASSERT_TRUE(idx != NULL);

    float vecs[4] = {
        1.0f, 0.0f,
        0.0f, 1.0f,
    };
    uint64_t ids[2] = {10, 20};
    faiss_index_add(idx, 2, vecs, ids);

    float query[2] = {1.0f, 0.0f};
    float dists[1];
    uint64_t out_ids[1];
    int found = faiss_index_search(idx, 1, query, 1, dists, out_ids);
    ASSERT_TRUE(found == 1);
    ASSERT_TRUE(out_ids[0] == 10);
    ASSERT_FLOAT_NEAR(1.0f, dists[0], 0.01f);

    faiss_index_destroy(idx);
    PASS();
}

static void test_faiss_empty_index(void) {
    void *idx = faiss_index_create(4);
    ASSERT_TRUE(idx != NULL);

    float query[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float dists[1];
    uint64_t out_ids[1];
    int found = faiss_index_search(idx, 1, query, 1, dists, out_ids);
    ASSERT_TRUE(found == 0);

    faiss_index_destroy(idx);
    PASS();
}

int main(void) {
    printf("=== NPC FAISS Tests ===\n\n");
    RUN(test_faiss_create_destroy);
    RUN(test_faiss_add_search);
    RUN(test_faiss_cosine_similarity);
    RUN(test_faiss_empty_index);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
