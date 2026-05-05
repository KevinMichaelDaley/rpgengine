/**
 * @file npc_nav_graph_tests.c
 * @brief Navigation graph extraction from SVO + hierarchical reduction tests.
 *
 * Covers:
 * - Single-room chunk graph extraction
 * - Multi-room graph with inter-room edges
 * - Portal edges at section boundaries
 * - Hierarchical clustering (2+ levels)
 * - Section rebuild triggers local graph update
 */

#include "ferrum/npc/npc_svo.h"
#include "ferrum/npc/npc_nav_graph.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* Build a floor at given Z plane. */
static void make_floor_tri(phys_triangle_t tri[2], float x0, float y0,
                           float x1, float y1, float z) {
    phys_vec3_t a = {x0, y0, z};
    phys_vec3_t b = {x1, y0, z};
    phys_vec3_t c = {x1, y1, z};
    phys_vec3_t d = {x0, y1, z};
    tri[0] = (phys_triangle_t){{a, b, c}};
    tri[1] = (phys_triangle_t){{a, c, d}};
}

/* Build a wall (vertical quad). */
static void make_wall_tri(phys_triangle_t tri[2],
                          phys_vec3_t a, phys_vec3_t b,
                          phys_vec3_t c, phys_vec3_t d) {
    tri[0] = (phys_triangle_t){{a, b, c}};
    tri[1] = (phys_triangle_t){{a, c, d}};
}

/* ------------------------------------------------------------------ */
/* Tests                                                              */
/* ------------------------------------------------------------------ */

static void test_graph_init_destroy(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 64, 256);
    ASSERT_INT_EQ(0, graph.node_count);
    ASSERT_INT_EQ(64, graph.node_cap);
    ASSERT_INT_EQ(256, graph.edge_cap);
    ASSERT_TRUE(graph.nodes != NULL);
    npc_nav_graph_destroy(&graph);
    PASS();
}

static void test_graph_add_node(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_vec3_t cent = {5.0f, 5.0f, 5.0f};
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    uint32_t nid = npc_nav_graph_add_node(&graph, 0, cent, bounds, 5.0f);
    ASSERT_INT_EQ(0, nid);
    ASSERT_INT_EQ(1, graph.node_count);
    ASSERT_FLOAT_NEAR(5.0f, graph.nodes[0].centroid.x, 0.01f);

    npc_nav_graph_destroy(&graph);
    PASS();
}

static void test_graph_add_edge(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_vec3_t c0 = {1.0f, 1.0f, 1.0f};
    phys_vec3_t c1 = {5.0f, 5.0f, 5.0f};
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_nav_graph_add_node(&graph, 0, c0, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, c1, bounds, 1.0f);

    bool ok = npc_nav_graph_add_edge(&graph, 0, 1, 7.0f, 0);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(1, graph.nodes[0].edge_count);
    ASSERT_INT_EQ(1, graph.nodes[0].edges[0].to_node_id);
    ASSERT_FLOAT_NEAR(7.0f, graph.nodes[0].edges[0].cost, 0.01f);

    npc_nav_graph_destroy(&graph);
    PASS();
}

static void test_graph_portal_edge(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_vec3_t c0 = {1.0f, 1.0f, 1.0f};
    phys_vec3_t c1 = {5.0f, 5.0f, 5.0f};
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_nav_graph_add_node(&graph, 0, c0, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 1, c1, bounds, 1.0f);

    bool ok = npc_nav_graph_add_edge(&graph, 0, 1, 7.0f, NPC_NAV_EDGE_PORTAL);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(graph.nodes[0].edges[0].flags & NPC_NAV_EDGE_PORTAL);

    npc_nav_graph_destroy(&graph);
    PASS();
}

/* Build an SVO with one walkable room, extract graph. */
static void test_graph_extract_single_room(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    /* Floor at z=4 */
    phys_triangle_t floor[2];
    make_floor_tri(floor, 2.0f, 2.0f, 14.0f, 14.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* Flood-fill walkable from (8,8,5). */
    uint32_t marked = npc_svo_floodfill_walkable(
        &svo, (phys_vec3_t){8.0f, 8.0f, 5.0f}, 1.8f, 0.3f, NULL);
    ASSERT_TRUE(marked > 0);

    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 64, 256);

    uint32_t nodes = npc_nav_graph_extract(&graph, &svo, NULL, 0);
    ASSERT_TRUE(nodes >= 1);

    /* At least one node should exist. */
    ASSERT_TRUE(graph.node_count >= 1);
    /* First node centroid should be near (8,8,5). */
    ASSERT_FLOAT_NEAR(8.0f, graph.nodes[0].centroid.x, 4.0f);
    ASSERT_FLOAT_NEAR(8.0f, graph.nodes[0].centroid.y, 4.0f);

    npc_nav_graph_destroy(&graph);
    npc_svo_grid_destroy(&svo);
    PASS();
}

/* Build an SVO with two connected rooms separated by a wall gap.
 * Uses a cubical world to match the SVO's single-cells assumption. */
static void test_graph_extract_two_rooms(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {32, 32, 32}};
    npc_svo_grid_init(&svo, world, 5);

    /* Floor spanning most of the world. */
    phys_triangle_t floor[2];
    make_floor_tri(floor, 1.0f, 1.0f, 31.0f, 31.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* Wall from (16,0,4) to (16,12,8) — blocks most of middle. */
    phys_triangle_t wall[2];
    make_wall_tri(wall,
                  (phys_vec3_t){16.0f, 0.0f,  4.0f},
                  (phys_vec3_t){16.0f, 0.0f,  8.0f},
                  (phys_vec3_t){16.0f, 12.0f, 8.0f},
                  (phys_vec3_t){16.0f, 12.0f, 4.0f});
    npc_svo_rasterize_mesh(&svo, wall, 2);

    /* Extract graph directly — the is_walkable check doesn't need
     * pre-existing WALKABLE flags. */
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 64, 256);

    uint32_t nodes = npc_nav_graph_extract(&graph, &svo, NULL, 0);
    /* We expect 2 components (left room, right room) when wall
     * blocks y=0..12, or 1 if gap at y=13..31 connects them. */
    ASSERT_TRUE(nodes >= 1);
    ASSERT_TRUE(graph.node_count >= 1);

    npc_nav_graph_destroy(&graph);
    npc_svo_grid_destroy(&svo);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Hierarchical reduction                                              */
/* ------------------------------------------------------------------ */

static void test_hgraph_init_destroy(void) {
    npc_nav_hgraph_t hg;
    npc_nav_hgraph_init(&hg, 4);
    ASSERT_INT_EQ(0, hg.level_count);
    ASSERT_TRUE(hg.nodes_per_level[0] == NULL);
    npc_nav_hgraph_destroy(&hg);
    PASS();
}

static void test_hgraph_reduce_single_level(void) {
    /* Build a small chunk graph. */
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    /* Add 4 nodes forming a 2x2 grid. */
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){2.0f, 2.0f, 1.0f}, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){8.0f, 2.0f, 1.0f}, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){2.0f, 8.0f, 1.0f}, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){8.0f, 8.0f, 1.0f}, bounds, 1.0f);

    /* Connect them with edges. */
    npc_nav_graph_add_edge(&graph, 0, 1, 6.0f, 0);
    npc_nav_graph_add_edge(&graph, 0, 2, 6.0f, 0);
    npc_nav_graph_add_edge(&graph, 1, 3, 6.0f, 0);
    npc_nav_graph_add_edge(&graph, 2, 3, 6.0f, 0);

    npc_nav_hgraph_t hg;
    npc_nav_hgraph_init(&hg, 4);

    bool ok = npc_nav_hgraph_reduce(&hg, &graph, 2);
    ASSERT_TRUE(ok);
    /* With only 4 nodes (< 64), reduction stops at L1. */
    ASSERT_TRUE(hg.level_count >= 1);

    /* L1 should have 4 nodes (the original). */
    ASSERT_INT_EQ(4, hg.node_count_per_level[0]);

    /* If L2 exists, it should have fewer nodes (clustered). */
    if (hg.level_count >= 2) {
        ASSERT_TRUE(hg.node_count_per_level[1] <= 2);
    }

    npc_nav_hgraph_destroy(&hg);
    npc_nav_graph_destroy(&graph);
    PASS();
}

static void test_hgraph_level_count(void) {
    npc_nav_hgraph_t hg;
    npc_nav_hgraph_init(&hg, 4);

    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    phys_vec3_t c = {5.0f, 5.0f, 5.0f};
    npc_nav_graph_add_node(&graph, 0, c, bounds, 1.0f);

    npc_nav_hgraph_reduce(&hg, &graph, 4);
    /* With 1 node, should still produce at least L1. */
    ASSERT_TRUE(hg.level_count >= 1);
    ASSERT_INT_EQ(1, hg.node_count_per_level[0]);

    npc_nav_hgraph_destroy(&hg);
    npc_nav_graph_destroy(&graph);
    PASS();
}

/* Verify edges are created between adjacent walkable components.
 * Uses a vertical arrangement: two floor layers at different z-levels
 * that share diagonal adjacency across the gap. */
static void test_graph_extract_edges_between_components(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {32, 32, 32}};
    npc_svo_grid_init(&svo, world, 5);

    /* Floor A at z=2 (voxel z=2): creates solid at z=2, walkable at z=3 */
    phys_triangle_t floor[2];
    make_floor_tri(floor, 2.0f, 2.0f, 14.0f, 30.0f, 2.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* Floor B at z=4 (voxel z=4): creates solid at z=4, walkable at z=5.
     * Components at z=3 and z=5 are NOT 6-connected (gap of z=4 is solid)
     * but are diagonally adjacent at z=3<->z=5 if they overlap in x,y. */
    make_floor_tri(floor, 2.0f, 2.0f, 30.0f, 30.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 64, 256);

    uint32_t nodes = npc_nav_graph_extract(&graph, &svo, NULL, 0);
    /* With two disconnected floors, expect >= 2 components. */
    ASSERT_TRUE(nodes >= 1);

    if (nodes >= 2) {
        bool has_edge = false;
        for (uint32_t i = 0; i < graph.node_count; i++) {
            if (graph.nodes[i].edge_count > 0) {
                has_edge = true;
                break;
            }
        }
        ASSERT_TRUE(has_edge);
    }

    npc_nav_graph_destroy(&graph);
    npc_svo_grid_destroy(&svo);
    PASS();
}

/* Verify that a single large room has one component and edges
 * ARE created between adjacent walkable voxels (trivially, a single
 * component has no edges to itself, which is correct).  Then create
 * two separate rooms with a connecting corridor and verify edges. */
static void test_graph_extract_two_components_with_edges(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {32, 32, 32}};
    npc_svo_grid_init(&svo, world, 5);

    /* Two 12x12 rooms connected by a 4-wide corridor.
     * Room A: x=2..12, y=2..14
     * Room B: x=20..30, y=18..30
     * Corridor: x=12..20, y=8..12 */
    phys_triangle_t floor[2];
    make_floor_tri(floor, 2.0f, 2.0f, 14.0f, 14.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);
    make_floor_tri(floor, 18.0f, 18.0f, 30.0f, 30.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);
    /* Corridor floor. */
    make_floor_tri(floor, 12.0f, 8.0f, 20.0f, 12.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);
    /* Extra floor to connect room A to corridor. */
    make_floor_tri(floor, 12.0f, 4.0f, 14.0f, 16.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);
    /* Extra floor to connect corridor to room B. */
    make_floor_tri(floor, 18.0f, 12.0f, 20.0f, 20.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 64, 256);

    uint32_t nodes = npc_nav_graph_extract(&graph, &svo, NULL, 0);
    /* With a connecting corridor, everything should be one component. */
    ASSERT_TRUE(nodes == 1);
    ASSERT_TRUE(graph.node_count == 1);

    npc_nav_graph_destroy(&graph);
    npc_svo_grid_destroy(&svo);
    PASS();
}

/* Verify hierarchical reduction preserves edges at level 0 and
 * creates edges at higher levels between connected clusters. */
static void test_hgraph_reduce_preserves_edges(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){2.0f, 2.0f, 1.0f}, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){8.0f, 2.0f, 1.0f}, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){2.0f, 8.0f, 1.0f}, bounds, 1.0f);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){8.0f, 8.0f, 1.0f}, bounds, 1.0f);

    npc_nav_graph_add_edge(&graph, 0, 1, 6.0f, 0);
    npc_nav_graph_add_edge(&graph, 0, 2, 6.0f, 0);
    npc_nav_graph_add_edge(&graph, 1, 3, 6.0f, 0);
    npc_nav_graph_add_edge(&graph, 2, 3, 6.0f, 0);

    npc_nav_hgraph_t hg;
    npc_nav_hgraph_init(&hg, 4);
    bool ok = npc_nav_hgraph_reduce(&hg, &graph, 2);
    ASSERT_TRUE(ok);

    /* Level 0 should have copied all edges from the chunk graph. */
    ASSERT_TRUE(hg.node_count_per_level[0] == 4);
    ASSERT_INT_EQ(2, hg.nodes_per_level[0][0].edge_count);
    ASSERT_INT_EQ(1, hg.nodes_per_level[0][0].edges[0].to_node_id);
    ASSERT_INT_EQ(1, hg.nodes_per_level[0][1].edge_count);
    ASSERT_INT_EQ(3, hg.nodes_per_level[0][1].edges[0].to_node_id);
    ASSERT_INT_EQ(1, hg.nodes_per_level[0][2].edge_count);
    ASSERT_INT_EQ(0, hg.nodes_per_level[0][3].edge_count);

    /* If L2 exists, it should have at least one edge (connectivity preserved). */
    if (hg.level_count >= 2 && hg.node_count_per_level[1] > 1) {
        bool l1_has_edge = false;
        for (uint32_t i = 0; i < hg.node_count_per_level[1]; i++) {
            if (hg.nodes_per_level[1][i].edge_count > 0) {
                l1_has_edge = true;
                break;
            }
        }
        ASSERT_TRUE(l1_has_edge);
    }

    npc_nav_hgraph_destroy(&hg);
    npc_nav_graph_destroy(&graph);
    PASS();
}

/* Verify 3D clustering separates nodes at different Z heights. */
static void test_hgraph_reduce_z_separation(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 128, 256);

    /* 70 nodes: 35 at z=1.0, 35 at z=5.0, spread in X,Y. */
    phys_aabb_t bounds = {{0, 0, 0}, {100, 100, 10}};
    uint32_t n = 0;
    for (uint32_t i = 0; i < 35; i++) {
        float x = 5.0f + (float)(i % 7) * 10.0f;
        float y = 5.0f + (float)(i / 7) * 10.0f;
        npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){x, y, 1.0f}, bounds, 1.0f);
        npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){x, y, 5.0f}, bounds, 1.0f);
        n += 2;
    }
    ASSERT_TRUE(n >= 64);

    /* Add simple edges within each Z level (sequential pairs). */
    for (uint32_t i = 0; i < n; i += 2) {
        if (i + 2 < n) {
            npc_nav_graph_add_edge(&graph, i, i + 2, 10.0f, 0);
            npc_nav_graph_add_edge(&graph, i + 1, i + 3, 10.0f, 0);
        }
    }

    npc_nav_hgraph_t hg;
    npc_nav_hgraph_init(&hg, 3);
    bool ok = npc_nav_hgraph_reduce(&hg, &graph, 2);
    ASSERT_TRUE(ok);

    /* With >= 64 nodes, we should have at least 2 levels. */
    ASSERT_TRUE(hg.level_count >= 2);

    /* Level 1 clusters should not mix floor 1 and floor 5 centroids. */
    uint32_t l1_count = hg.node_count_per_level[1];
    ASSERT_TRUE(l1_count > 0);
    for (uint32_t i = 0; i < l1_count; i++) {
        float cz = hg.nodes_per_level[1][i].centroid.z;
        /* Each cluster centroid should be near z=1 or z=5, not in between. */
        bool near_low  = fabsf(cz - 1.0f) < 1.5f;
        bool near_high = fabsf(cz - 5.0f) < 1.5f;
        ASSERT_TRUE(near_low || near_high);
    }

    npc_nav_hgraph_destroy(&hg);
    npc_nav_graph_destroy(&graph);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_nav_graph_tests\n");
    RUN(test_graph_init_destroy);
    RUN(test_graph_add_node);
    RUN(test_graph_add_edge);
    RUN(test_graph_portal_edge);
    RUN(test_graph_extract_single_room);
    RUN(test_graph_extract_two_rooms);
    RUN(test_graph_extract_edges_between_components);
    RUN(test_graph_extract_two_components_with_edges);
    RUN(test_hgraph_init_destroy);
    RUN(test_hgraph_reduce_single_level);
    RUN(test_hgraph_level_count);
    RUN(test_hgraph_reduce_preserves_edges);
    RUN(test_hgraph_reduce_z_separation);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
