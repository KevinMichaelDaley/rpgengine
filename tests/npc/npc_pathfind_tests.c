/**
 * @file npc_pathfind_tests.c
 * @brief Pathfinding tests: SVO A*, graph A*, hierarchical A*, shortcut.
 */

#include "ferrum/npc/npc_svo.h"
#include "ferrum/npc/npc_nav_graph.h"
#include "ferrum/npc/npc_pathfind.h"
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
static void make_floor(phys_triangle_t tri[2], float x0, float y0,
                       float x1, float y1, float z) {
    phys_vec3_t a = {x0, y0, z};
    phys_vec3_t b = {x1, y0, z};
    phys_vec3_t c = {x1, y1, z};
    phys_vec3_t d = {x0, y1, z};
    tri[0] = (phys_triangle_t){{a, b, c}};
    tri[1] = (phys_triangle_t){{a, c, d}};
}

/* Build a wall quad. */
static void make_wall(phys_triangle_t tri[2],
                      phys_vec3_t a, phys_vec3_t b,
                      phys_vec3_t c, phys_vec3_t d) {
    tri[0] = (phys_triangle_t){{a, b, c}};
    tri[1] = (phys_triangle_t){{a, c, d}};
}

/* ------------------------------------------------------------------ */
/* SVO A* tests                                                       */
/* ------------------------------------------------------------------ */

static void test_svo_astar_straight_line(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    /* Floor at z=4. */
    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* Path from (2,2,5) to (14,14,5) on the same floor. */
    phys_vec3_t start = {2.0f, 2.0f, 5.0f};
    phys_vec3_t goal = {14.0f, 14.0f, 5.0f};

    phys_vec3_t waypoints[64];
    uint32_t wp_count = 0;
    bool found = npc_svo_astar(&svo, NULL, 0, start, goal,
                               waypoints, &wp_count, 64, 0.3f, 1.8f);
    ASSERT_TRUE(found);
    ASSERT_TRUE(wp_count >= 2);

    /* First waypoint should be near start. */
    ASSERT_FLOAT_NEAR(start.x, waypoints[0].x, 3.0f);

    npc_svo_grid_destroy(&svo);
    PASS();
}

static void test_svo_astar_unreachable(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    /* Floor only on left side. */
    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 7.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* Path across the gap (right side has no floor). */
    phys_vec3_t start = {2.0f, 8.0f, 5.0f};
    phys_vec3_t goal = {14.0f, 8.0f, 5.0f};

    phys_vec3_t waypoints[64];
    uint32_t wp_count = 0;
    bool found = npc_svo_astar(&svo, NULL, 0, start, goal,
                               waypoints, &wp_count, 64, 0.3f, 1.8f);
    ASSERT_TRUE(!found);

    npc_svo_grid_destroy(&svo);
    PASS();
}

static void test_svo_astar_around_obstacle(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    /* Floor covering the world. */
    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* Wall obstacle at x=8, z=4..8, blocking direct path. */
    phys_triangle_t wall[2];
    make_wall(wall,
              (phys_vec3_t){8.0f, 2.0f, 4.0f},
              (phys_vec3_t){8.0f, 2.0f, 8.0f},
              (phys_vec3_t){8.0f, 8.0f, 8.0f},
              (phys_vec3_t){8.0f, 8.0f, 4.0f});
    npc_svo_rasterize_mesh(&svo, wall, 2);

    /* Path from (4,5,5) to (12,5,5) must go around wall. */
    phys_vec3_t start = {4.0f, 5.0f, 5.0f};
    phys_vec3_t goal = {12.0f, 5.0f, 5.0f};

    phys_vec3_t waypoints[64];
    uint32_t wp_count = 0;
    bool found = npc_svo_astar(&svo, NULL, 0, start, goal,
                               waypoints, &wp_count, 64, 0.3f, 1.8f);
    ASSERT_TRUE(found);
    ASSERT_TRUE(wp_count >= 3); /* longer path around obstacle */

    /* Path should contain a waypoint with x near 4 (start side)
     * and one with x near 12 (goal side). */
    bool has_start_side = false;
    bool has_goal_side = false;
    for (uint32_t i = 0; i < wp_count; i++) {
        float wx = waypoints[i].x;
        if (wx < 6.0f) has_start_side = true;
        if (wx > 10.0f) has_goal_side = true;
    }
    ASSERT_TRUE(has_start_side);
    ASSERT_TRUE(has_goal_side);

    npc_svo_grid_destroy(&svo);
    PASS();
}

/* ------------------------------------------------------------------ */
/* Graph A* tests                                                     */
/* ------------------------------------------------------------------ */

static void test_graph_astar_basic(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){1, 1, 1}, bounds, 1);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){5, 5, 5}, bounds, 1);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){9, 9, 9}, bounds, 1);

    npc_nav_graph_add_edge(&graph, 0, 1, 5.0f, 0);
    npc_nav_graph_add_edge(&graph, 1, 2, 5.0f, 0);

    uint32_t out_nodes[16];
    uint32_t out_count = 0;
    bool found = npc_graph_astar(&graph, 0, 2, out_nodes, &out_count, 16);
    ASSERT_TRUE(found);
    ASSERT_INT_EQ(3, out_count);
    ASSERT_INT_EQ(0, out_nodes[0]);
    ASSERT_INT_EQ(1, out_nodes[1]);
    ASSERT_INT_EQ(2, out_nodes[2]);

    npc_nav_graph_destroy(&graph);
    PASS();
}

static void test_graph_astar_unreachable(void) {
    npc_nav_graph_t graph;
    npc_nav_graph_init(&graph, 16, 64);

    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){1, 1, 1}, bounds, 1);
    npc_nav_graph_add_node(&graph, 0, (phys_vec3_t){9, 9, 9}, bounds, 1);
    /* No edge between them. */

    uint32_t out_nodes[16];
    uint32_t out_count = 0;
    bool found = npc_graph_astar(&graph, 0, 1, out_nodes, &out_count, 16);
    ASSERT_TRUE(!found);

    npc_nav_graph_destroy(&graph);
    PASS();
}

/* ------------------------------------------------------------------ */
/* LOS shortcutting                                                   */
/* ------------------------------------------------------------------ */

static void test_pathfind_shortcut(void) {
    phys_vec3_t in[8];
    in[0] = (phys_vec3_t){0, 0, 1};
    in[1] = (phys_vec3_t){1, 0, 1};
    in[2] = (phys_vec3_t){2, 0, 1};
    in[3] = (phys_vec3_t){3, 0, 1};
    in[4] = (phys_vec3_t){4, 0, 1};
    in[5] = (phys_vec3_t){5, 0, 1};

    phys_vec3_t out[8];
    uint32_t out_count = 0;
    npc_pathfind_shortcut(in, 6, out, &out_count, 8, NULL, NULL, 0);
    /* Straight line: should reduce to just start+end. */
    ASSERT_INT_EQ(2, out_count);
    ASSERT_FLOAT_NEAR(0, out[0].x, 0.01f);
    ASSERT_FLOAT_NEAR(5, out[1].x, 0.01f);
    PASS();
}

static void test_pathfind_shortcut_corner(void) {
    /* L-shaped path cannot be shortcutted. */
    phys_vec3_t in[3];
    in[0] = (phys_vec3_t){0, 0, 1};
    in[1] = (phys_vec3_t){5, 0, 1};
    in[2] = (phys_vec3_t){5, 5, 1};

    phys_vec3_t out[8];
    uint32_t out_count = 0;
    npc_pathfind_shortcut(in, 3, out, &out_count, 8, NULL, NULL, 0);
    /* L-shape: all 3 waypoints should remain. */
    ASSERT_INT_EQ(3, out_count);
    PASS();
}

static void test_pathfind_shortcut_empty_corridor(void) {
    /* Straight path through empty SVO: should collapse to start+end. */
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    phys_vec3_t in[6];
    in[0] = (phys_vec3_t){2.0f, 8.0f, 5.0f};
    in[1] = (phys_vec3_t){4.0f, 8.0f, 5.0f};
    in[2] = (phys_vec3_t){6.0f, 8.0f, 5.0f};
    in[3] = (phys_vec3_t){8.0f, 8.0f, 5.0f};
    in[4] = (phys_vec3_t){10.0f, 8.0f, 5.0f};
    in[5] = (phys_vec3_t){12.0f, 8.0f, 5.0f};

    phys_vec3_t out[8];
    uint32_t out_count = 0;
    npc_pathfind_shortcut(in, 6, out, &out_count, 8, &svo, NULL, 0);
    ASSERT_INT_EQ(2, out_count);
    ASSERT_FLOAT_NEAR(in[0].x, out[0].x, 0.01f);
    ASSERT_FLOAT_NEAR(in[5].x, out[1].x, 0.01f);

    npc_svo_grid_destroy(&svo);
    PASS();
}

static void test_pathfind_shortcut_around_wall(void) {
    /* Path going around a wall: corner waypoint must be retained. */
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    phys_triangle_t wall[2];
    make_wall(wall,
              (phys_vec3_t){8.0f, 4.0f, 4.0f},
              (phys_vec3_t){8.0f, 4.0f, 8.0f},
              (phys_vec3_t){8.0f, 6.0f, 8.0f},
              (phys_vec3_t){8.0f, 6.0f, 4.0f});
    npc_svo_rasterize_mesh(&svo, wall, 2);

    /* Path detours around the wall via y=2. */
    phys_vec3_t in[4];
    in[0] = (phys_vec3_t){4.0f, 5.0f, 5.0f};
    in[1] = (phys_vec3_t){6.0f, 5.0f, 5.0f};
    in[2] = (phys_vec3_t){6.0f, 2.0f, 5.0f};
    in[3] = (phys_vec3_t){12.0f, 2.0f, 5.0f};

    phys_vec3_t out[8];
    uint32_t out_count = 0;
    npc_pathfind_shortcut(in, 4, out, &out_count, 8, &svo, NULL, 0);
    /* The corner at in[2] is the detour point around the wall.
     * LOS from in[0] to in[3] must pass through the wall voxels. */
    ASSERT_TRUE(out_count >= 3);
    bool has_corner = false;
    for (uint32_t i = 0; i < out_count; i++) {
        if (fabsf(out[i].x - 6.0f) < 0.1f && fabsf(out[i].y - 2.0f) < 0.1f) {
            has_corner = true;
            break;
        }
    }
    ASSERT_TRUE(has_corner);

    npc_svo_grid_destroy(&svo);
    PASS();
}

static void test_pathfind_shortcut_dynamic_blocker(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {16, 16, 16}};
    npc_svo_grid_init(&svo, world, 4);

    phys_triangle_t floor[2];
    make_floor(floor, 1.0f, 1.0f, 15.0f, 15.0f, 4.0f);
    npc_svo_rasterize_mesh(&svo, floor, 2);

    /* A blocker sitting at x=6..8, y=7..9, z=4..6. */
    npc_svo_blocker_t blk;
    blk.bounds = (phys_aabb_t){{5.0f, 7.0f, 4.0f}, {7.0f, 9.0f, 6.0f}};
    blk.section_id = 0xFFFFFFFFu;
    blk.flags = 0;

    phys_vec3_t in[4];
    in[0] = (phys_vec3_t){4.0f, 8.0f, 5.0f};
    in[1] = (phys_vec3_t){7.0f, 8.0f, 5.0f};
    in[2] = (phys_vec3_t){9.0f, 8.0f, 5.0f};
    in[3] = (phys_vec3_t){12.0f, 8.0f, 5.0f};

    phys_vec3_t out[8];
    uint32_t out_count = 0;
    npc_pathfind_shortcut(in, 4, out, &out_count, 8, &svo, &blk, 1);
    /* The blocker covers mid-section, so LOS from in[0] to in[2]
     * or in[3] should be blocked. in[1] should be retained. */
    ASSERT_TRUE(out_count >= 3);

    npc_svo_grid_destroy(&svo);
    PASS();
}

/* Helper: box mesh (same as in svo tests). */
static void make_box_mesh_pf(phys_triangle_t *out_tris,
                             phys_vec3_t min, phys_vec3_t max) {
    phys_vec3_t v[8];
    v[0] = (phys_vec3_t){min.x, min.y, min.z};
    v[1] = (phys_vec3_t){max.x, min.y, min.z};
    v[2] = (phys_vec3_t){max.x, max.y, min.z};
    v[3] = (phys_vec3_t){min.x, max.y, min.z};
    v[4] = (phys_vec3_t){min.x, min.y, max.z};
    v[5] = (phys_vec3_t){max.x, min.y, max.z};
    v[6] = (phys_vec3_t){max.x, max.y, max.z};
    v[7] = (phys_vec3_t){min.x, max.y, max.z};

    int idx = 0;
    /* bottom */
    out_tris[idx++] = (phys_triangle_t){{v[0], v[1], v[2]}};
    out_tris[idx++] = (phys_triangle_t){{v[0], v[2], v[3]}};
    /* top */
    out_tris[idx++] = (phys_triangle_t){{v[4], v[6], v[5]}};
    out_tris[idx++] = (phys_triangle_t){{v[4], v[7], v[6]}};
    /* front */
    out_tris[idx++] = (phys_triangle_t){{v[0], v[5], v[1]}};
    out_tris[idx++] = (phys_triangle_t){{v[0], v[4], v[5]}};
    /* back */
    out_tris[idx++] = (phys_triangle_t){{v[3], v[2], v[6]}};
    out_tris[idx++] = (phys_triangle_t){{v[3], v[6], v[7]}};
    /* left */
    out_tris[idx++] = (phys_triangle_t){{v[0], v[3], v[7]}};
    out_tris[idx++] = (phys_triangle_t){{v[0], v[7], v[4]}};
    /* right */
    out_tris[idx++] = (phys_triangle_t){{v[1], v[5], v[6]}};
    out_tris[idx++] = (phys_triangle_t){{v[1], v[6], v[2]}};
}

/* ── Agent-dimension A* tests ────────────────────────────────────── */

/* Wide agent (radius=1.0, voxel_size=0.625 → horiz=1) cannot path
 * through a 1-voxel-wide corridor flanked by solid walls.  A narrow
 * agent (radius=0.3, horiz=0) can pass. */
static void test_svo_astar_wide_agent_narrow_corridor(void) {
    npc_svo_grid_t svo;
    phys_aabb_t world = {{0, 0, 0}, {10, 10, 10}};
    npc_svo_grid_init(&svo, world, 4);

    /* Thick floor: vz=4,5,6 become SOLID. */
    phys_triangle_t floor_a[12], floor_b[12];
    make_box_mesh_pf(floor_a, (phys_vec3_t){1.0f, 1.0f, 3.0f},
                              (phys_vec3_t){9.0f, 9.0f, 3.4f});
    make_box_mesh_pf(floor_b, (phys_vec3_t){1.0f, 1.0f, 3.4f},
                              (phys_vec3_t){9.0f, 9.0f, 3.8f});
    npc_svo_rasterize_mesh(&svo, floor_a, 12);
    npc_svo_rasterize_mesh(&svo, floor_b, 12);

    /* Two wall boxes at x=2.6-3.1 and x=3.8-4.3 create a 1‑voxel
     * corridor at vx=5 (x ≈ 3.125–3.75).  They extend from y=3
     * to y=8 so they block the full y-range the path traverses. */
    phys_triangle_t wall_l[12], wall_r[12];
    make_box_mesh_pf(wall_l, (phys_vec3_t){2.6f, 3.0f, 3.5f},
                           (phys_vec3_t){3.1f, 8.0f, 6.0f});
    make_box_mesh_pf(wall_r, (phys_vec3_t){3.8f, 3.0f, 3.5f},
                           (phys_vec3_t){4.3f, 8.0f, 6.0f});
    npc_svo_rasterize_mesh(&svo, wall_l, 12);
    npc_svo_rasterize_mesh(&svo, wall_r, 12);

    /* Wide agent (radius=1.0 → horiz=1): corridor voxels at vx=5
     * have SOLID neighbours at vx=4 and vx=6 → no path. */
    phys_vec3_t waypoints_w[64];
    uint32_t wp_w = 0;
    bool found_w = npc_svo_astar(&svo, NULL, 0,
                                  (phys_vec3_t){3.4f, 4.5f, 4.5f},
                                  (phys_vec3_t){3.4f, 6.5f, 4.5f},
                                  waypoints_w, &wp_w, 64, 1.0f, 0.5f);
    ASSERT_TRUE(!found_w);

    /* Narrow agent (radius=0.3 → horiz=0): no horizontal clearance
     * required → path found. */
    phys_vec3_t waypoints_n[64];
    uint32_t wp_n = 0;
    bool found_n = npc_svo_astar(&svo, NULL, 0,
                                  (phys_vec3_t){3.4f, 4.5f, 4.5f},
                                  (phys_vec3_t){3.4f, 6.5f, 4.5f},
                                  waypoints_n, &wp_n, 64, 0.3f, 0.5f);
    ASSERT_TRUE(found_n);

    npc_svo_grid_destroy(&svo);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_pathfind_tests\n");
    RUN(test_svo_astar_straight_line);
    RUN(test_svo_astar_unreachable);
    RUN(test_svo_astar_around_obstacle);
    RUN(test_graph_astar_basic);
    RUN(test_graph_astar_unreachable);
    RUN(test_pathfind_shortcut);
    RUN(test_pathfind_shortcut_corner);
    RUN(test_pathfind_shortcut_empty_corridor);
    RUN(test_pathfind_shortcut_around_wall);
    RUN(test_pathfind_shortcut_dynamic_blocker);
    RUN(test_svo_astar_wide_agent_narrow_corridor);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
