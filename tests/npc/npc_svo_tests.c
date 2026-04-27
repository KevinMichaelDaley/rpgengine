/**
 * @file npc_svo_tests.c
 * @brief SVO grid construction, query, and blocker overlay tests.
 */

#include "ferrum/npc/npc_svo.h"
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

/* Helper: build a simple axis-aligned box mesh (12 triangles). */
static void make_box_mesh(phys_triangle_t *out_tris, phys_vec3_t min, phys_vec3_t max) {
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

/* Helper: build a single horizontal floor quad at z = z_plane. */
static void make_floor(phys_triangle_t *out_tris, float z_plane) {
    phys_vec3_t a = {1.0f, 1.0f, z_plane};
    phys_vec3_t b = {9.0f, 1.0f, z_plane};
    phys_vec3_t c = {9.0f, 9.0f, z_plane};
    phys_vec3_t d = {1.0f, 9.0f, z_plane};
    out_tris[0] = (phys_triangle_t){{a, b, c}};
    out_tris[1] = (phys_triangle_t){{a, c, d}};
}

/* ── Tests ──────────────────────────────────────────────────────── */

static void test_svo_init_destroy(void) {
    npc_svo_grid_t grid;
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    bool ok = npc_svo_grid_init(&grid, bounds, 4);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(1, (int)grid.node_count); /* reserved null node */
    ASSERT_INT_EQ(4, (int)grid.max_depth);
    ASSERT_FLOAT_NEAR(0.625f, grid.voxel_size, 0.001f);
    npc_svo_grid_destroy(&grid);
    PASS();
}

static void test_svo_rasterize_box(void) {
    npc_svo_grid_t grid;
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_svo_grid_init(&grid, bounds, 4);

    phys_triangle_t tris[12];
    make_box_mesh(tris, (phys_vec3_t){4, 4, 4}, (phys_vec3_t){6, 6, 6});
    npc_svo_rasterize_mesh(&grid, tris, 12);

    /* A point on the box surface shell should be solid
     * (surface rasterization does not fill the interior). */
    uint32_t node;
    uint8_t flags = npc_svo_query_point(&grid, (phys_vec3_t){5, 5, 4.1f}, &node);
    ASSERT_TRUE(flags & NPC_SVO_FLAG_SOLID);
    ASSERT_TRUE(node != NPC_SVO_INVALID_NODE);

    /* The interior of the box is NOT solid (only surface triangles). */
    flags = npc_svo_query_point(&grid, (phys_vec3_t){5, 5, 5}, &node);
    ASSERT_TRUE((flags & NPC_SVO_FLAG_SOLID) == 0);

    /* Outside the box should not be solid. */
    flags = npc_svo_query_point(&grid, (phys_vec3_t){2, 2, 2}, &node);
    ASSERT_TRUE((flags & NPC_SVO_FLAG_SOLID) == 0);

    npc_svo_grid_destroy(&grid);
    PASS();
}

static void test_svo_query_point(void) {
    npc_svo_grid_t grid;
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_svo_grid_init(&grid, bounds, 4);

    phys_triangle_t tris[12];
    make_box_mesh(tris, (phys_vec3_t){4, 4, 4}, (phys_vec3_t){6, 6, 6});
    npc_svo_rasterize_mesh(&grid, tris, 12);

    /* Corner of the box should be solid (conservative rasterization). */
    uint8_t f = npc_svo_query_point(&grid, (phys_vec3_t){4.1f, 4.1f, 4.1f}, NULL);
    ASSERT_TRUE(f & NPC_SVO_FLAG_SOLID);

    /* Far corner should be empty. */
    f = npc_svo_query_point(&grid, (phys_vec3_t){9, 9, 9}, NULL);
    ASSERT_TRUE((f & NPC_SVO_FLAG_SOLID) == 0);

    /* Out of bounds should return 0. */
    f = npc_svo_query_point(&grid, (phys_vec3_t){-1, 5, 5}, NULL);
    ASSERT_INT_EQ(0, (int)f);

    npc_svo_grid_destroy(&grid);
    PASS();
}

static void test_svo_floodfill(void) {
    npc_svo_grid_t grid;
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_svo_grid_init(&grid, bounds, 4);

    /* Build a floor at z = 4. */
    phys_triangle_t floor[2];
    make_floor(floor, 4.0f);
    npc_svo_rasterize_mesh(&grid, floor, 2);

    /* Floor voxels should be solid before flood-fill. */
    uint8_t f = npc_svo_query_point(&grid, (phys_vec3_t){5, 5, 4}, NULL);
    ASSERT_TRUE(f & NPC_SVO_FLAG_SOLID);

    /* Flood-fill from a point just above the floor. */
    uint32_t marked = npc_svo_floodfill_walkable(
        &grid, (phys_vec3_t){5, 5, 4.7f}, 1.8f, 0.3f);
    ASSERT_TRUE(marked > 0);

    /* The voxel directly above the floor should now be walkable. */
    f = npc_svo_query_point(&grid, (phys_vec3_t){5, 5, 4.7f}, NULL);
    ASSERT_TRUE(f & NPC_SVO_FLAG_WALKABLE);

    /* A voxel deep below the floor should NOT be walkable. */
    f = npc_svo_query_point(&grid, (phys_vec3_t){5, 5, 2}, NULL);
    ASSERT_TRUE((f & NPC_SVO_FLAG_WALKABLE) == 0);

    npc_svo_grid_destroy(&grid);
    PASS();
}

static void test_svo_voxel_blocked(void) {
    npc_svo_grid_t grid;
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_svo_grid_init(&grid, bounds, 4);

    npc_svo_blocker_t blockers[2];
    blockers[0].bounds = (phys_aabb_t){{3, 3, 3}, {5, 5, 5}};
    blockers[0].section_id = 0xFFFFFFFFu;
    blockers[0].flags = 0;

    blockers[1].bounds = (phys_aabb_t){{7, 7, 7}, {9, 9, 9}};
    blockers[1].section_id = 0xFFFFFFFFu;
    blockers[1].flags = 0;

    /* Voxel inside blocker 0 should be blocked. */
    uint32_t cells = 1u << 4;
    uint32_t vx = (uint32_t)(4.0f / 10.0f * (float)cells);
    uint32_t vy = (uint32_t)(4.0f / 10.0f * (float)cells);
    uint32_t vz = (uint32_t)(4.0f / 10.0f * (float)cells);
    bool blocked = npc_svo_voxel_blocked(&grid, blockers, 2, vx, vy, vz);
    ASSERT_TRUE(blocked);

    /* Voxel far from both blockers should not be blocked. */
    vx = (uint32_t)(1.0f / 10.0f * (float)cells);
    vy = (uint32_t)(1.0f / 10.0f * (float)cells);
    vz = (uint32_t)(1.0f / 10.0f * (float)cells);
    blocked = npc_svo_voxel_blocked(&grid, blockers, 2, vx, vy, vz);
    ASSERT_TRUE(!blocked);

    npc_svo_grid_destroy(&grid);
    PASS();
}

static void test_svo_world_to_voxel(void) {
    npc_svo_grid_t grid;
    phys_aabb_t bounds = {{0, 0, 0}, {10, 10, 10}};
    npc_svo_grid_init(&grid, bounds, 4);

    uint32_t x, y, z;
    bool ok = npc_svo_world_to_voxel(&grid, (phys_vec3_t){5, 5, 5}, &x, &y, &z);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(8, (int)x); /* 5/10 * 16 = 8 */
    ASSERT_INT_EQ(8, (int)y);
    ASSERT_INT_EQ(8, (int)z);

    ok = npc_svo_world_to_voxel(&grid, (phys_vec3_t){-1, 5, 5}, &x, &y, &z);
    ASSERT_TRUE(!ok);

    npc_svo_grid_destroy(&grid);
    PASS();
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("npc_svo_tests\n");
    RUN(test_svo_init_destroy);
    RUN(test_svo_rasterize_box);
    RUN(test_svo_query_point);
    RUN(test_svo_floodfill);
    RUN(test_svo_voxel_blocked);
    RUN(test_svo_world_to_voxel);
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
