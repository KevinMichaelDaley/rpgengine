/**
 * @file srd_seed_init_tests.c
 * @brief Tests for srd_seed_to_grid: seed layout → SDF grid + room map.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_seed_init.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>

/* ── Test harness ──────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    int _e = (exp), _a = (act); \
    if (_e != _a) { \
        fprintf(stderr, "  FAIL %s:%d: expected %d, got %d\n", \
                __FILE__, __LINE__, _e, _a); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_LT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a < _b)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f < %.6f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_GT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a > _b)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f > %.6f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b); \
        return 1; \
    } \
} while (0)

/* ── Flood-fill helper ─────────────────────────────────────────── */

/**
 * @brief 3D flood-fill on negative SDF voxels from a seed point.
 *
 * Returns the number of voxels reached. Uses a simple stack-based
 * approach with a visited grid. Useful for verifying doorway connectivity.
 */
static int flood_fill_count(const srd_sdf_grid_t *grid,
                            int sx, int sy, int sz) {
    int total = grid->nx * grid->ny * grid->nz;
    uint8_t *visited = (uint8_t *)calloc((size_t)total, 1);
    if (!visited) return -1;

    /* Simple stack (worst case: every voxel) */
    int *stack = (int *)malloc((size_t)total * 3 * sizeof(int));
    if (!stack) { free(visited); return -1; }

    int sp = 0;
    int count = 0;

    /* Push seed */
    if (srd_sdf_grid_get(grid, sx, sy, sz) < 0.0f) {
        stack[sp * 3 + 0] = sx;
        stack[sp * 3 + 1] = sy;
        stack[sp * 3 + 2] = sz;
        sp++;
        visited[sz * grid->ny * grid->nx + sy * grid->nx + sx] = 1;
    }

    static const int DX[6] = { 1, -1,  0,  0,  0,  0};
    static const int DY[6] = { 0,  0,  1, -1,  0,  0};
    static const int DZ[6] = { 0,  0,  0,  0,  1, -1};

    while (sp > 0) {
        sp--;
        int x = stack[sp * 3 + 0];
        int y = stack[sp * 3 + 1];
        int z = stack[sp * 3 + 2];
        count++;

        for (int d = 0; d < 6; d++) {
            int nx = x + DX[d];
            int ny = y + DY[d];
            int nz = z + DZ[d];
            if (nx < 0 || nx >= grid->nx ||
                ny < 0 || ny >= grid->ny ||
                nz < 0 || nz >= grid->nz)
                continue;
            int idx = nz * grid->ny * grid->nx + ny * grid->nx + nx;
            if (!visited[idx] && grid->values[idx] < 0.0f) {
                visited[idx] = 1;
                stack[sp * 3 + 0] = nx;
                stack[sp * 3 + 1] = ny;
                stack[sp * 3 + 2] = nz;
                sp++;
            }
        }
    }

    free(stack);
    free(visited);
    return count;
}

/* ── Test: single room ─────────────────────────────────────────── */

static int test_single_room(void) {
    /* One 4x3x4 meter room centered at (4, 2, 4). */
    srd_seed_room_t rooms[1] = {{
        .cx = 4.0f, .cy = 2.0f, .cz = 4.0f,
        .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
        .type = SRD_ROOM_ENTRANCE
    }};

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    int rc = srd_seed_to_grid(rooms, 1, NULL, 0, 0.25f, 1.0f,
                              &grid, &map);
    ASSERT_INT_EQ(0, rc);

    /* Grid should have been created */
    ASSERT_TRUE(grid.values != NULL);
    ASSERT_TRUE(map.ids != NULL);
    ASSERT_INT_EQ(1, map.n_rooms);

    /* Room center should be inside (negative SDF) */
    int vx, vy, vz;
    srd_sdf_grid_world_to_voxel(&grid, 4.0f, 2.0f, 4.0f, &vx, &vy, &vz);
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, vx, vy, vz), 0.0f);

    /* Room center should have room_id = 1 */
    ASSERT_INT_EQ(1, srd_room_map_get(&map, vx, vy, vz));

    /* Room type should be ENTRANCE */
    ASSERT_INT_EQ(SRD_ROOM_ENTRANCE, srd_room_map_get_type(&map, 1));

    /* Outside the room should be positive */
    srd_sdf_grid_world_to_voxel(&grid, 0.0f, 0.0f, 0.0f, &vx, &vy, &vz);
    ASSERT_FLOAT_GT(srd_sdf_grid_get(&grid, vx, vy, vz), 0.0f);

    /* Room volume > 0 */
    ASSERT_TRUE(srd_room_map_count_volume(&map, 1) > 0);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: two adjacent rooms with doorway ─────────────────────── */

static int test_two_rooms_doorway(void) {
    /* Room 1: center (3, 2, 4), half (2, 1.5, 2) → spans X=[1,5]
     * Room 2: center (7, 2, 4), half (2, 1.5, 2) → spans X=[5,9]
     * Adjacent along X at x=5. Doorway should connect them. */
    srd_seed_room_t rooms[2] = {
        { .cx = 3.0f, .cy = 2.0f, .cz = 4.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_ENTRANCE },
        { .cx = 7.0f, .cy = 2.0f, .cz = 4.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_GENERIC }
    };

    /* Adjacency: room 0 ↔ room 1 */
    int adj_pairs[2] = {0, 1};

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    int rc = srd_seed_to_grid(rooms, 2, adj_pairs, 1, 0.25f, 1.0f,
                              &grid, &map);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(2, map.n_rooms);

    /* Both room centers should be inside */
    int vx1, vy1, vz1, vx2, vy2, vz2;
    srd_sdf_grid_world_to_voxel(&grid, 3.0f, 2.0f, 4.0f, &vx1, &vy1, &vz1);
    srd_sdf_grid_world_to_voxel(&grid, 7.0f, 2.0f, 4.0f, &vx2, &vy2, &vz2);
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, vx1, vy1, vz1), 0.0f);
    ASSERT_FLOAT_LT(srd_sdf_grid_get(&grid, vx2, vy2, vz2), 0.0f);

    /* Different room IDs */
    uint8_t id1 = srd_room_map_get(&map, vx1, vy1, vz1);
    uint8_t id2 = srd_room_map_get(&map, vx2, vy2, vz2);
    ASSERT_TRUE(id1 != id2);
    ASSERT_TRUE(id1 > 0);
    ASSERT_TRUE(id2 > 0);

    /* Rooms should be adjacent in the room map */
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, id1, id2));

    /* Flood-fill from room 1 center should reach room 2 center
     * (doorway connects them through the shared wall) */
    int reached = flood_fill_count(&grid, vx1, vy1, vz1);
    ASSERT_TRUE(reached > 0);

    /* Verify room 2 center is reachable by checking it's in the same
     * connected component */
    int reached2 = flood_fill_count(&grid, vx2, vy2, vz2);
    ASSERT_TRUE(reached2 > 0);

    /* Both flood-fills should find the same number of voxels
     * (single connected component) */
    ASSERT_INT_EQ(reached, reached2);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: three rooms in L-shape ──────────────────────────────── */

static int test_three_rooms_l_shape(void) {
    /* Room 0: center (3, 2, 3), half (2, 1.5, 2) — left
     * Room 1: center (7, 2, 3), half (2, 1.5, 2) — right, adj to 0
     * Room 2: center (7, 2, 7), half (2, 1.5, 2) — below right, adj to 1
     * Forms an L-shape. 0↔1 and 1↔2, but 0 is NOT adjacent to 2. */
    srd_seed_room_t rooms[3] = {
        { .cx = 3.0f, .cy = 2.0f, .cz = 3.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_ENTRANCE },
        { .cx = 7.0f, .cy = 2.0f, .cz = 3.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_GENERIC },
        { .cx = 7.0f, .cy = 2.0f, .cz = 7.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_BOSS }
    };

    int adj_pairs[4] = {0, 1, 1, 2};

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    int rc = srd_seed_to_grid(rooms, 3, adj_pairs, 2, 0.25f, 1.0f,
                              &grid, &map);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(3, map.n_rooms);

    /* Verify types */
    ASSERT_INT_EQ(SRD_ROOM_ENTRANCE, srd_room_map_get_type(&map, 1));
    ASSERT_INT_EQ(SRD_ROOM_GENERIC,  srd_room_map_get_type(&map, 2));
    ASSERT_INT_EQ(SRD_ROOM_BOSS,     srd_room_map_get_type(&map, 3));

    /* Adjacency: 1↔2 yes, 2↔3 yes, 1↔3 no */
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, 1, 2));
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, 2, 3));
    ASSERT_TRUE(!srd_room_map_are_adjacent(&map, 1, 3));

    /* All three rooms should be flood-fill connected */
    int vx0, vy0, vz0;
    srd_sdf_grid_world_to_voxel(&grid, 3.0f, 2.0f, 3.0f, &vx0, &vy0, &vz0);
    int total_reached = flood_fill_count(&grid, vx0, vy0, vz0);

    int vx2, vy2, vz2;
    srd_sdf_grid_world_to_voxel(&grid, 7.0f, 2.0f, 7.0f, &vx2, &vy2, &vz2);
    int reached_from_2 = flood_fill_count(&grid, vx2, vy2, vz2);

    /* Same connected component */
    ASSERT_INT_EQ(total_reached, reached_from_2);

    /* Each room should have positive volume */
    ASSERT_TRUE(srd_room_map_count_volume(&map, 1) > 0);
    ASSERT_TRUE(srd_room_map_count_volume(&map, 2) > 0);
    ASSERT_TRUE(srd_room_map_count_volume(&map, 3) > 0);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: no adjacency (disconnected rooms) ───────────────────── */

static int test_disconnected_rooms(void) {
    /* Two rooms with no adjacency — should NOT be flood-fill connected. */
    srd_seed_room_t rooms[2] = {
        { .cx = 3.0f, .cy = 2.0f, .cz = 3.0f,
          .hx = 1.0f, .hy = 1.0f, .hz = 1.0f,
          .type = SRD_ROOM_GENERIC },
        { .cx = 8.0f, .cy = 2.0f, .cz = 8.0f,
          .hx = 1.0f, .hy = 1.0f, .hz = 1.0f,
          .type = SRD_ROOM_GENERIC }
    };

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    int rc = srd_seed_to_grid(rooms, 2, NULL, 0, 0.25f, 1.0f,
                              &grid, &map);
    ASSERT_INT_EQ(0, rc);

    /* Rooms should NOT be adjacent */
    ASSERT_TRUE(!srd_room_map_are_adjacent(&map, 1, 2));

    /* Flood-fill from room 1 should NOT reach room 2 */
    int vx1, vy1, vz1, vx2, vy2, vz2;
    srd_sdf_grid_world_to_voxel(&grid, 3.0f, 2.0f, 3.0f, &vx1, &vy1, &vz1);
    srd_sdf_grid_world_to_voxel(&grid, 8.0f, 2.0f, 8.0f, &vx2, &vy2, &vz2);

    int count1 = flood_fill_count(&grid, vx1, vy1, vz1);
    int count2 = flood_fill_count(&grid, vx2, vy2, vz2);

    /* Different connected components → different counts
     * (unless by coincidence they have the same volume, which they do
     * since they're the same size. Check that count1 < total negative) */
    int total_neg = srd_sdf_grid_count_negative(&grid);
    ASSERT_TRUE(count1 < total_neg);
    ASSERT_TRUE(count2 < total_neg);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: null/invalid inputs ─────────────────────────────────── */

static int test_null_inputs(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;

    ASSERT_INT_EQ(-1, srd_seed_to_grid(NULL, 0, NULL, 0, 0.25f, 1.0f,
                                        &grid, &map));

    srd_seed_room_t rooms[1] = {{
        .cx = 0.0f, .cy = 0.0f, .cz = 0.0f,
        .hx = 1.0f, .hy = 1.0f, .hz = 1.0f,
        .type = SRD_ROOM_GENERIC
    }};

    ASSERT_INT_EQ(-1, srd_seed_to_grid(rooms, 1, NULL, 0, 0.25f, 1.0f,
                                        NULL, &map));
    ASSERT_INT_EQ(-1, srd_seed_to_grid(rooms, 1, NULL, 0, 0.25f, 1.0f,
                                        &grid, NULL));
    ASSERT_INT_EQ(-1, srd_seed_to_grid(rooms, 0, NULL, 0, 0.25f, 1.0f,
                                        &grid, &map));
    return 0;
}

/* ── Test: voxel size and grid dimensions ──────────────────────── */

static int test_grid_dimensions(void) {
    /* Room at (4, 2, 4), half (2, 1.5, 2).
     * Spans [2,6] x [0.5,3.5] x [2,6].
     * With 1m margin: [1,7] x [-0.5,4.5] x [1,7].
     * At 0.5m voxels: 12 x 10 x 12 minimum.
     * Grid origin should be at (1, -0.5, 1) or nearby. */
    srd_seed_room_t rooms[1] = {{
        .cx = 4.0f, .cy = 2.0f, .cz = 4.0f,
        .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
        .type = SRD_ROOM_GENERIC
    }};

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    srd_seed_to_grid(rooms, 1, NULL, 0, 0.5f, 1.0f, &grid, &map);

    /* Grid should be at least large enough to contain the room + margin */
    ASSERT_TRUE(grid.nx >= 12);
    ASSERT_TRUE(grid.ny >= 10);
    ASSERT_TRUE(grid.nz >= 12);

    /* Grid dimensions should match room map dimensions */
    ASSERT_INT_EQ(grid.nx, map.nx);
    ASSERT_INT_EQ(grid.ny, map.ny);
    ASSERT_INT_EQ(grid.nz, map.nz);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"single_room",           test_single_room},
    {"two_rooms_doorway",     test_two_rooms_doorway},
    {"three_rooms_l_shape",   test_three_rooms_l_shape},
    {"disconnected_rooms",    test_disconnected_rooms},
    {"null_inputs",           test_null_inputs},
    {"grid_dimensions",       test_grid_dimensions},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;

    fprintf(stderr, "srd_seed_init_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        struct test_case *tc = &TESTS[i];
        fprintf(stderr, "  RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            fprintf(stderr, "  OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "  FAIL %s\n", tc->name);
        }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
