#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "ferrum/procgen/srd/srd_loss_compiler.h"
#include "ferrum/procgen/procgen_srd_types.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_NEAR(a, b, eps) ASSERT_TRUE(fabsf((a)-(b)) <= (eps))
#define PASS() g_pass++

static void test_parse_simple(void) {
    const char *loss =
        "LOSS:\n"
        "  MinimumSize(bar_rooms, 6)\n"
        "  NonPenetration(all)\n";

    srd_loss_term_t terms[16];
    uint32_t count = 0;
    int rc = srd_loss_compile(loss, NULL, 0, terms, 16, &count);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 2);
    ASSERT_INT_EQ(terms[0].primitive, FR_LOSS_MINIMUM_SIZE);
    ASSERT_INT_EQ(terms[1].primitive, FR_LOSS_NON_PENETRATION);
    PASS();
}

static void test_parse_with_operators(void) {
    const char *loss =
        "LOSS:\n"
        "  PathDistance(entrance, treasure) > 30\n"
        "  Separation(B, P) < 20\n";

    srd_loss_term_t terms[16];
    uint32_t count = 0;
    int rc = srd_loss_compile(loss, NULL, 0, terms, 16, &count);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 2);
    ASSERT_INT_EQ(terms[0].op, 0);  /* > */
    ASSERT_NEAR(terms[0].target_value, 30.0f, 0.01f);
    ASSERT_INT_EQ(terms[1].op, 1);  /* < */
    PASS();
}

static void test_parse_with_labels(void) {
    /* Provide a room graph with labeled rooms */
    fr_graph_node_t nodes[3];
    memset(nodes, 0, sizeof(nodes));
    nodes[0].type_char = 'B'; strcpy(nodes[0].label, "bar");
    nodes[1].type_char = 'R'; strcpy(nodes[1].label, "treasure");
    nodes[2].type_char = 'G'; strcpy(nodes[2].label, "entrance");

    fr_room_graph_t graph;
    fr_room_graph_init(&graph);
    graph.nodes = nodes; graph.node_count = 3;

    const char *loss =
        "LOSS:\n"
        "  PathDistance(entrance, treasure) > 20\n";

    srd_loss_term_t terms[16];
    uint32_t count = 0;
    int rc = srd_loss_compile(loss, &graph, 0, terms, 16, &count);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(count, 1);
    ASSERT_INT_EQ(terms[0].label_indices[0], 2);  /* entrance = node 2 */
    ASSERT_INT_EQ(terms[0].label_indices[1], 1);  /* treasure = node 1 */
    PASS();
}

static void test_parse_all_scope(void) {
    fr_graph_node_t nodes[2];
    memset(nodes, 0, sizeof(nodes));
    nodes[0].type_char = 'R'; strcpy(nodes[0].label, "");
    nodes[1].type_char = 'B'; strcpy(nodes[1].label, "");

    fr_room_graph_t graph;
    fr_room_graph_init(&graph);
    graph.nodes = nodes; graph.node_count = 2;

    const char *loss =
        "LOSS:\n"
        "  MinimumSize(all, 6)\n";

    srd_loss_term_t terms[16];
    uint32_t count = 0;
    srd_loss_compile(loss, &graph, 0, terms, 16, &count);
    ASSERT_INT_EQ(count, 1);
    ASSERT_INT_EQ(terms[0].all_rooms, 1);
    PASS();
}

int main(void) {
    printf("=== Loss Compiler Tests ===\n\n");

    RUN(test_parse_simple);
    RUN(test_parse_with_operators);
    RUN(test_parse_with_labels);
    RUN(test_parse_all_scope);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
