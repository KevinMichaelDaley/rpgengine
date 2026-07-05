#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "ferrum/procgen/procgen_srd_types.h"
#include "ferrum/procgen/procgen_ascii_parse.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

static const char *SIMPLE_TWO_FLOOR =
    "=== FLOOR 0: GROUND ===\n"
    "W W W W W\n"
    "W R R R W\n"
    "W R R R W\n"
    "W W G W W\n"
    "=== FLOOR 1: UPPER ===\n"
    "W W W W W\n"
    "W P P P W\n"
    "W P P P W\n"
    "W W W W W\n";

static void test_parse_two_floor_grid(void) {
    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(SIMPLE_TWO_FLOOR, &graph);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(graph.node_count > 0);
    ASSERT_TRUE(graph.edge_count > 0);
    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_parse_counts(void) {
    /* Floor 0 has: 3x3 R region = 9 cells, 1 G cell
       Floor 1 has: 3x3 P region = 9 cells
       Total nodes: 1 (R) + 1 (G) + 1 (P) = 3
       Edges: R-G on floor 0 (adjacent: R cells at (1,2) touch G at (2,3)...),
              minimal: 1 edge. Floor 1: no edges (only P and walls).
       Stairs: none (no ^ or v characters)
    */
    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(SIMPLE_TWO_FLOOR, &graph);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(graph.node_count, 3);
    ASSERT_INT_EQ(graph.edge_count, 1);
    ASSERT_INT_EQ(graph.stair_pair_count, 0);

    /* Verify node types */
    int found_r = 0, found_g = 0, found_p = 0;
    for (uint32_t i = 0; i < graph.node_count; i++) {
        switch (graph.nodes[i].type_char) {
            case 'R': found_r++; break;
            case 'G': found_g++; break;
            case 'P': found_p++; break;
        }
    }
    ASSERT_INT_EQ(found_r, 1);
    ASSERT_INT_EQ(found_g, 1);
    ASSERT_INT_EQ(found_p, 1);

    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_parse_stair_anchors(void) {
    const char *grid =
        "=== FLOOR 0 ===\n"
        "W W W W W\n"
        "W R R ^ W\n"
        "W R R R W\n"
        "W W W W W\n"
        "=== FLOOR 1 ===\n"
        "W W W W W\n"
        "W P P v W\n"
        "W P P P W\n"
        "W W W W W\n";

    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(grid, &graph);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(graph.stair_pair_count, 1);

    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_parse_dot_merged(void) {
    /* '.' should be merged into adjacent room regions, not create own nodes */
    const char *grid =
        "=== FLOOR 0 ===\n"
        "W W W W W\n"
        "W R R . W\n"
        "W R R . W\n"
        "W W W W W\n";

    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(grid, &graph);
    ASSERT_INT_EQ(rc, 0);
    /* Should be 1 node (R region expanded by . cells) */
    ASSERT_INT_EQ(graph.node_count, 1);
    ASSERT_INT_EQ(graph.nodes[0].type_char, 'R');

    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_parse_empty_grid(void) {
    const char *grid = "=== FLOOR 0 ===\nW W W\nW W W\nW W W\n";
    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(grid, &graph);
    ASSERT_INT_EQ(rc, 0);
    ASSERT_INT_EQ(graph.node_count, 0);
    ASSERT_INT_EQ(graph.edge_count, 0);
    fr_room_graph_destroy(&graph);
    PASS();
}

static void test_parse_invalid_no_header(void) {
    const char *bad = "W W W\nW R W\nW W W\n";
    fr_room_graph_t graph;
    int rc = procgen_ascii_parse(bad, &graph);
    ASSERT_INT_EQ(rc, -1);
    PASS();
}

int main(void) {
    printf("=== ASCII Parse Tests ===\n\n");

    RUN(test_parse_two_floor_grid);
    RUN(test_parse_counts);
    RUN(test_parse_stair_anchors);
    RUN(test_parse_dot_merged);
    RUN(test_parse_empty_grid);
    RUN(test_parse_invalid_no_header);

    printf("=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
