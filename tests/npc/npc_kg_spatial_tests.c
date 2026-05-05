/**
 * @file npc_kg_spatial_tests.c
 * @brief Knowledge graph spatial/reachability tests.
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

static void test_new_relations(void) {
    uint32_t near_id = npc_kg_relation_id("near");
    uint32_t inside_id = npc_kg_relation_id("inside");
    uint32_t adj_id = npc_kg_relation_id("adjacent_to");
    uint32_t vis_id = npc_kg_relation_id("visible_from");
    uint32_t reach_id = npc_kg_relation_id("reachable_from");
    uint32_t path_id = npc_kg_relation_id("path_to");

    ASSERT_TRUE(strcmp(npc_kg_relation_name(near_id), "near") == 0);
    ASSERT_TRUE(strcmp(npc_kg_relation_name(inside_id), "inside") == 0);
    ASSERT_TRUE(strcmp(npc_kg_relation_name(adj_id), "adjacent_to") == 0);
    ASSERT_TRUE(strcmp(npc_kg_relation_name(vis_id), "visible_from") == 0);
    ASSERT_TRUE(strcmp(npc_kg_relation_name(reach_id), "reachable_from") == 0);
    ASSERT_TRUE(strcmp(npc_kg_relation_name(path_id), "path_to") == 0);

    PASS();
}

static void test_spatial_edge_upsert(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 1, NPC_KG_NODE_LOCATION, emb);
    npc_kg_insert_node(&kg, 2, NPC_KG_NODE_LOCATION, emb);

    uint32_t near_rel = npc_kg_relation_id("near");
    bool ok = npc_kg_upsert_spatial_edge(&kg, 1, 2, near_rel, 0.9f, 1000);
    ASSERT_TRUE(ok);

    npc_kg_node_t *n = &kg.nodes[0];
    ASSERT_INT_EQ(1, n->edge_count);
    ASSERT_INT_EQ(2, n->edges[0].to_node_id);
    ASSERT_TRUE(n->edges[0].flags & NPC_KG_EDGE_SPATIAL);

    ok = npc_kg_upsert_spatial_edge(&kg, 1, 2, near_rel, 0.5f, 2000);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(1, n->edge_count);
    ASSERT_FLOAT_NEAR(0.5f, n->edges[0].weight, 0.001f);
    ASSERT_TRUE(n->edges[0].flags & NPC_KG_EDGE_SPATIAL);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_spatial_decay_faster(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 1, NPC_KG_NODE_ENTITY, emb);

    uint32_t saw_rel = npc_kg_relation_id("saw_at");
    uint32_t near_rel = npc_kg_relation_id("near");

    npc_kg_add_edge(&kg, 1, 2, saw_rel, 1.0f, 0);
    npc_kg_upsert_spatial_edge(&kg, 1, 3, near_rel, 1.0f, 0);

    npc_kg_spatial_decay(&kg, 100.0f, 0.05f);

    float spatial_weight = kg.nodes[0].edges[1].weight;
    float social_weight = kg.nodes[0].edges[0].weight;

    ASSERT_FLOAT_NEAR(1.0f, social_weight, 0.001f);
    ASSERT_TRUE(spatial_weight < 1.0f);
    ASSERT_TRUE(spatial_weight > 0.0f);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_set_reachable_creates_edge(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 10, NPC_KG_NODE_LOCATION, emb);
    npc_kg_insert_node(&kg, 20, NPC_KG_NODE_LOCATION, emb);

    vec3_t waypoints[1] = {{1.0f, 2.0f, 3.0f}};
    npc_kg_set_reachable(&kg, 10, 20, 12.5f, waypoints, 1);

    npc_kg_node_t *n = &kg.nodes[0];
    ASSERT_INT_EQ(1, n->edge_count);
    ASSERT_INT_EQ(20, n->edges[0].to_node_id);

    uint32_t reach_rel = npc_kg_relation_id("reachable_from");
    ASSERT_INT_EQ(reach_rel, n->edges[0].relation_id);
    ASSERT_TRUE(n->edges[0].flags & NPC_KG_EDGE_REACHABLE);
    ASSERT_FLOAT_NEAR(12.5f, n->edges[0].weight, 0.001f);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_set_reachable_stale_on_failure(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 10, NPC_KG_NODE_LOCATION, emb);
    npc_kg_insert_node(&kg, 20, NPC_KG_NODE_LOCATION, emb);

    npc_kg_set_reachable(&kg, 10, 20, -1.0f, NULL, 0);

    npc_kg_node_t *n = &kg.nodes[0];
    ASSERT_INT_EQ(1, n->edge_count);
    ASSERT_TRUE(n->edges[0].flags & NPC_KG_EDGE_STALE);
    ASSERT_TRUE(!(n->edges[0].flags & NPC_KG_EDGE_REACHABLE));

    npc_kg_destroy(&kg);
    PASS();
}

static void test_set_reachable_updates_existing(void) {
    npc_knowledge_graph_t kg;
    npc_kg_init(&kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(&kg, 10, NPC_KG_NODE_LOCATION, emb);
    npc_kg_insert_node(&kg, 20, NPC_KG_NODE_LOCATION, emb);

    npc_kg_set_reachable(&kg, 10, 20, 5.0f, NULL, 0);
    npc_kg_set_reachable(&kg, 10, 20, 8.0f, NULL, 0);

    npc_kg_node_t *n = &kg.nodes[0];
    ASSERT_INT_EQ(1, n->edge_count);
    ASSERT_TRUE(n->edges[0].flags & NPC_KG_EDGE_REACHABLE);
    ASSERT_FLOAT_NEAR(8.0f, n->edges[0].weight, 0.001f);

    npc_kg_destroy(&kg);
    PASS();
}

int main(void) {
    printf("=== NPC KG Spatial & Reachability Tests ===\n\n");
    RUN(test_new_relations);
    RUN(test_spatial_edge_upsert);
    RUN(test_spatial_decay_faster);
    RUN(test_set_reachable_creates_edge);
    RUN(test_set_reachable_stale_on_failure);
    RUN(test_set_reachable_updates_existing);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
