/**
 * @file npc_knowledge_graph_tests.c
 * @brief Knowledge graph init/insert/search/decay tests.
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
#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        printf("FAIL (%s:%d) expected %d got %d\n", \
               __FILE__, __LINE__, (int)(exp), (int)(act)); \
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

static void test_kg_init_destroy(void) {
    npc_knowledge_graph_t kg;
    bool ok = npc_kg_init(&kg, 16, 4);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(0, kg.node_count);
    ASSERT_INT_EQ(16, kg.node_cap);
    ASSERT_INT_EQ(4, kg.embedding_dim);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_kg_insert_node(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    npc_kg_node_t *n = npc_kg_insert_node(&kg, 42, NPC_KG_NODE_ENTITY, emb);
    ASSERT_TRUE(n != NULL);
    ASSERT_INT_EQ(42, n->node_id);
    ASSERT_INT_EQ(NPC_KG_NODE_ENTITY, n->type);
    ASSERT_TRUE(n->embedding != NULL);
    ASSERT_FLOAT_NEAR(1.0f, n->embedding[0], 0.001f);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_kg_add_edge(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 1, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(&kg, 2, NPC_KG_NODE_ENTITY, emb);

    uint32_t rel = npc_kg_relation_id("saw_at");
    bool ok = npc_kg_add_edge(&kg, 1, 2, rel, 0.8f, 1000000);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(1, kg.nodes[0].edge_count);
    ASSERT_INT_EQ(2, kg.nodes[0].edges[0].to_node_id);
    ASSERT_FLOAT_NEAR(0.8f, kg.nodes[0].edges[0].weight, 0.001f);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_kg_relation_lookup(void) {
    uint32_t rel1 = npc_kg_relation_id("saw_at");
    uint32_t rel2 = npc_kg_relation_id("saw_at");
    ASSERT_INT_EQ(rel1, rel2);

    const char *name = npc_kg_relation_name(rel1);
    ASSERT_TRUE(name != NULL);
    ASSERT_TRUE(strcmp(name, "saw_at") == 0);

    uint32_t rel3 = npc_kg_relation_id("custom_rel");
    const char *name3 = npc_kg_relation_name(rel3);
    ASSERT_TRUE(name3 != NULL);
    ASSERT_TRUE(strcmp(name3, "custom_rel") == 0);

    PASS();
}

static void test_kg_decay(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 1, NPC_KG_NODE_ENTITY, emb);
    npc_kg_add_edge(&kg, 1, 2, npc_kg_relation_id("saw_at"), 1.0f, 0);

    npc_kg_decay_edges(&kg, 100.0f, 0.01f);
    ASSERT_TRUE(kg.nodes[0].edges[0].weight < 1.0f);
    ASSERT_TRUE(kg.nodes[0].edges[0].weight > 0.0f);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_kg_search_with_faiss(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb1[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    float emb2[4] = {0.0f, 1.0f, 0.0f, 0.0f};
    npc_kg_insert_node(&kg, 10, NPC_KG_NODE_ENTITY, emb1);
    npc_kg_insert_node(&kg, 20, NPC_KG_NODE_ENTITY, emb2);

    kg.faiss_index = faiss_index_create(4);
    ASSERT_TRUE(kg.faiss_index != NULL);

    float all[8];
    memcpy(all, emb1, sizeof(emb1));
    memcpy(all + 4, emb2, sizeof(emb2));
    uint64_t ids[2] = {10, 20};
    faiss_index_add(kg.faiss_index, 2, all, ids);

    float query[4] = {1.0f, 0.0f, 0.0f, 0.0f};
    uint64_t out_ids[2];
    float out_scores[2];
    uint32_t found = npc_kg_search(&kg, query, 2, out_ids, out_scores);
    ASSERT_INT_EQ(2, found);
    ASSERT_INT_EQ(10, out_ids[0]);

    npc_kg_destroy(&kg);
    PASS();
}

int main(void) {
    printf("=== NPC Knowledge Graph Tests ===\n\n");
    RUN(test_kg_init_destroy);
    RUN(test_kg_insert_node);
    RUN(test_kg_add_edge);
    RUN(test_kg_relation_lookup);
    RUN(test_kg_decay);
    RUN(test_kg_search_with_faiss);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
