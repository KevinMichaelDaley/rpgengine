/**
 * @file npc_kg_astar_tests.c
 * @brief A* pathfinding, RELATED_ENTITIES, and KG_SHORTEST_PATH tests.
 */

#include "ferrum/npc/npc_knowledge_graph.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
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

/* ------------------------------------------------------------------ */
/* Helper: build a 5-node "trust chain" graph                          */
/*                                                                     */
/*   A(1) --[trusts, 0.9]--> B(2) --[trusts, 0.8]--> C(3)             */
/*   A(1) --[fears,   0.3]--> C(3)                                     */
/*   A(1) --[trusts,  0.1]--> D(4) --[trusts, 0.9]--> E(5)            */
/*   D(4) --[trusts,  0.1]--> E(5)                                     */
/*                                                                     */
/*   Shortest trust path from A to C: A(1)->B(2)->C(3) cost=1.7        */
/*   Shortest trust path from A to E: A(1)->D(4)->E(5) cost=0.2        */
/*   No path from C to A (outgoing edges only).                        */
/* ------------------------------------------------------------------ */

static void build_test_graph(npc_knowledge_graph_t *kg) {
    memset(kg, 0, sizeof(*kg));
    npc_kg_init(kg, 8, 4);

    float emb[4] = {0};
    npc_kg_insert_node(kg, 1, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(kg, 2, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(kg, 3, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(kg, 4, NPC_KG_NODE_ENTITY, emb);
    npc_kg_insert_node(kg, 5, NPC_KG_NODE_ENTITY, emb);

    uint32_t rel_trusts = npc_kg_relation_id("trusts");
    uint32_t rel_fears = npc_kg_relation_id("fears");

    npc_kg_add_edge(kg, 1, 2, rel_trusts, 0.9f, 0);
    npc_kg_add_edge(kg, 2, 3, rel_trusts, 0.8f, 0);
    npc_kg_add_edge(kg, 1, 3, rel_fears,  0.3f, 0);
    npc_kg_add_edge(kg, 1, 4, rel_trusts, 0.1f, 0);
    npc_kg_add_edge(kg, 4, 5, rel_trusts, 0.9f, 0);
    npc_kg_add_edge(kg, 4, 5, rel_trusts, 0.1f, 0);
}

static void free_path_result(npc_kg_path_result_t *r) {
    free(r->node_ids);
    free(r->relation_ids);
    r->node_ids = NULL;
    r->relation_ids = NULL;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

static void test_astar_shortest_trust_chain(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 1;
    req.goal_node_id  = 5;
    req.max_cost = 100.0f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(found);
    ASSERT_TRUE(res.found);
    ASSERT_INT_EQ(2, res.step_count);
    ASSERT_INT_EQ(1, res.node_ids[0]);
    ASSERT_INT_EQ(4, res.node_ids[1]);
    ASSERT_INT_EQ(5, res.node_ids[2]);

    uint32_t rel_trusts = npc_kg_relation_id("trusts");
    ASSERT_INT_EQ(rel_trusts, res.relation_ids[0]);
    ASSERT_INT_EQ(rel_trusts, res.relation_ids[1]);

    ASSERT_FLOAT_NEAR(0.2f, res.total_cost, 0.01f);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_astar_allowed_relations_whitelist(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 1;
    req.goal_node_id  = 3;

    /* Allow only 'trusts' — the path through B should be found. */
    uint32_t rel_trusts = npc_kg_relation_id("trusts");
    req.allowed_relations[0] = rel_trusts;
    req.allowed_relation_count = 1;
    req.max_cost = 100.0f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(found);
    ASSERT_TRUE(res.found);
    ASSERT_INT_EQ(2, res.step_count);
    ASSERT_INT_EQ(1, res.node_ids[0]);
    ASSERT_INT_EQ(2, res.node_ids[1]);
    ASSERT_INT_EQ(3, res.node_ids[2]);
    ASSERT_FLOAT_NEAR(1.7f, res.total_cost, 0.01f);

    free_path_result(&res);

    /* Allow only 'fears' — alternative direct path. */
    uint32_t rel_fears = npc_kg_relation_id("fears");
    req.allowed_relations[0] = rel_fears;
    req.allowed_relation_count = 1;

    memset(&res, 0, sizeof(res));
    found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(found);
    ASSERT_TRUE(res.found);
    ASSERT_INT_EQ(1, res.step_count);
    ASSERT_INT_EQ(1, res.node_ids[0]);
    ASSERT_INT_EQ(3, res.node_ids[1]);
    ASSERT_INT_EQ(rel_fears, res.relation_ids[0]);
    ASSERT_FLOAT_NEAR(0.3f, res.total_cost, 0.01f);

    free_path_result(&res);

    /* Allow multiple relations. */
    req.allowed_relations[0] = rel_trusts;
    req.allowed_relations[1] = rel_fears;
    req.allowed_relation_count = 2;

    memset(&res, 0, sizeof(res));
    found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(found);
    ASSERT_TRUE(res.found);
    /* Both paths work; shortest is the direct fears edge. */
    ASSERT_INT_EQ(1, res.step_count);
    ASSERT_FLOAT_NEAR(0.3f, res.total_cost, 0.01f);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_astar_unreachable(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 3; /* C has no outgoing edges */
    req.goal_node_id  = 1;
    req.max_cost = 100.0f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(!found || !res.found);
    ASSERT_TRUE(!res.found);
    ASSERT_INT_EQ(0, res.step_count);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_astar_goal_not_found(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 1;
    req.goal_node_id  = 99; /* non-existent */
    req.max_cost = 100.0f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(!found || !res.found);
    ASSERT_TRUE(!res.found);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_astar_max_cost_exceeded(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    /* Path A->D->E has total cost 0.2. Set max_cost too low. */
    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 1;
    req.goal_node_id  = 5;
    req.max_cost = 0.1f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(!found || !res.found);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_related_entities_lookup(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    /* Node 1 has edges: trusts->2, fears->3, trusts->4 */
    uint32_t rel_trusts = npc_kg_relation_id("trusts");

    /* Manually count edges from node 1 that match 'trusts'. */
    npc_kg_node_t *n = NULL;
    for (uint32_t i = 0; i < kg.node_count; i++) {
        if (kg.nodes[i].node_id == 1) {
            n = &kg.nodes[i];
            break;
        }
    }
    ASSERT_TRUE(n != NULL);

    uint32_t trust_count = 0;
    for (uint32_t j = 0; j < n->edge_count; j++) {
        if (n->edges[j].relation_id == rel_trusts) {
            trust_count++;
        }
    }
    ASSERT_INT_EQ(2, trust_count);

    /* Check specific related entities. */
    bool found_b = false, found_d = false;
    for (uint32_t j = 0; j < n->edge_count; j++) {
        if (n->edges[j].relation_id == rel_trusts) {
            if (n->edges[j].to_node_id == 2) found_b = true;
            if (n->edges[j].to_node_id == 4) found_d = true;
        }
    }
    ASSERT_TRUE(found_b);
    ASSERT_TRUE(found_d);

    npc_kg_destroy(&kg);
    PASS();
}

static void test_astar_large_graph(void) {
    npc_knowledge_graph_t kg;
    memset(&kg, 0, sizeof(kg));
    npc_kg_init(&kg, 300, 4);

    float emb[4] = {0};
    uint64_t i;
    for (i = 1; i <= 300; i++) {
        npc_kg_insert_node(&kg, i, NPC_KG_NODE_ENTITY, emb);
    }

    uint32_t rel_trusts = npc_kg_relation_id("trusts");

    for (i = 1; i < 300; i++) {
        npc_kg_add_edge(&kg, i, i + 1, rel_trusts, 0.5f, 0);
    }

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 1;
    req.goal_node_id  = 300;
    req.max_cost = 1000.0f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(found);
    ASSERT_TRUE(res.found);
    ASSERT_INT_EQ(299, res.step_count);
    ASSERT_FLOAT_NEAR(149.5f, res.total_cost, 0.01f);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

static void test_kg_shortest_path_chain(void) {
    npc_knowledge_graph_t kg;
    build_test_graph(&kg);

    npc_kg_path_request_t req;
    memset(&req, 0, sizeof(req));
    req.start_node_id = 4;
    req.goal_node_id  = 5;
    req.max_cost = 100.0f;

    npc_kg_path_result_t res;
    memset(&res, 0, sizeof(res));

    bool found = npc_kg_astar(&kg, &req, &res);
    ASSERT_TRUE(found);
    ASSERT_TRUE(res.found);
    ASSERT_INT_EQ(1, res.step_count);
    ASSERT_INT_EQ(4, res.node_ids[0]);
    ASSERT_INT_EQ(5, res.node_ids[1]);

    uint32_t rel_trusts = npc_kg_relation_id("trusts");
    ASSERT_INT_EQ(rel_trusts, res.relation_ids[0]);

    free_path_result(&res);
    npc_kg_destroy(&kg);
    PASS();
}

int main(void) {
    printf("=== NPC KG A* Traversal Tests ===\n\n");
    RUN(test_astar_shortest_trust_chain);
    RUN(test_astar_allowed_relations_whitelist);
    RUN(test_astar_unreachable);
    RUN(test_astar_goal_not_found);
    RUN(test_astar_max_cost_exceeded);
    RUN(test_related_entities_lookup);
    RUN(test_kg_shortest_path_chain);
    RUN(test_astar_large_graph);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
