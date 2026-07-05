#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/procgen_ascii_parse.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static const char *TOWER =
    "=== FLOOR 0: GROUND ===\n"
    "W W W W W W W W W W\n"
    "W B B B B R R R ^ W\n"
    "W B B B B R R R . W\n"
    "W R R R R R R R . W\n"
    "W W W W G W W W W W\n"
    "=== FLOOR 1: UPPER ===\n"
    "W W W W W W W W W W\n"
    "W P P P P P P P v W\n"
    "W P P P P P P P . W\n"
    "W P P P P P P P . W\n"
    "W W W W W W W W W W\n";

static void test_parse_tower(void) {
    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(TOWER, &graph);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(graph.node_count >= 5);  /* B + R + G + . merged into R + P + stairs */
    ASSERT_TRUE(graph.edge_count >= 3);  /* B-R, R-G, P-stair connections */
    ASSERT_INT_EQ(graph.stair_pair_count, 1); /* ^ on floor 0 matched with v on floor 1 */

    /* Count node types */
    int types[256] = {0};
    for (uint32_t i = 0; i < graph.node_count; i++)
        types[(unsigned char)graph.nodes[i].type_char]++;

    ASSERT_INT_EQ(types['B'], 1);
    ASSERT_INT_EQ(types['G'], 1);
    ASSERT_INT_EQ(types['P'], 1);
    ASSERT_INT_EQ(types['R'], 1);  /* all R cells connected via . on floor 0 */
    ASSERT_TRUE(types['^'] + types['v'] >= 1);

    /* Verify stair pair connects floors */
    ASSERT_TRUE(graph.stair_pair_count > 0);
    uint32_t up_node   = graph.stair_pairs[0].stair_anchor_node;
    ASSERT_TRUE(up_node < graph.node_count);

    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_parse_tower_nodes_are_connected(void) {
    fr_room_graph_t graph;
    procgen_ascii_parse(TOWER, &graph);

    /* B should have an edge to R (adjacent cells in grid) */
    int b_id = -1, r_id = -1;
    for (uint32_t i = 0; i < graph.node_count; i++) {
        if (graph.nodes[i].type_char == 'B') b_id = i;
        if (graph.nodes[i].type_char == 'R') r_id = i;
    }
    ASSERT_TRUE(b_id >= 0 && r_id >= 0);

    int b_r_edge = 0;
    for (uint32_t e = 0; e < graph.edge_count; e++) {
        if ((graph.edges[e].node_a == (uint32_t)b_id && graph.edges[e].node_b == (uint32_t)r_id)
         || (graph.edges[e].node_a == (uint32_t)r_id && graph.edges[e].node_b == (uint32_t)b_id))
            b_r_edge = 1;
    }
    ASSERT_TRUE(b_r_edge);

    fr_room_graph_destroy(&graph);
    PASS();
}

int main(void) {
    printf("=== M1: ASCII Parse → RoomGraph Integration ===\n\n");

    RUN(test_parse_tower);
    RUN(test_parse_tower_nodes_are_connected);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
