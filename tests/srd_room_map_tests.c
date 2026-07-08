/**
 * @file srd_room_map_tests.c
 * @brief Tests for srd_room_map_t: room identity tracking over a voxel grid.
 *
 * TDD Phase 1 (RED): tests define the API before implementation exists.
 */
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"

#include <stdio.h>
#include <math.h>
#include <string.h>

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

#define ASSERT_TRUE_MSG(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s (%s)\n", __FILE__, __LINE__, #cond, msg); \
        return 1; \
    } \
} while (0)

/* ── Test: init and destroy ────────────────────────────────────── */

static int test_init_basic(void) {
    srd_room_map_t map;
    int rc = srd_room_map_init(&map, 8, 4, 8);
    ASSERT_INT_EQ(0, rc);
    ASSERT_INT_EQ(8, map.nx);
    ASSERT_INT_EQ(4, map.ny);
    ASSERT_INT_EQ(8, map.nz);
    ASSERT_INT_EQ(0, map.n_rooms);
    ASSERT_TRUE(map.ids != NULL);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_init_null(void) {
    ASSERT_INT_EQ(-1, srd_room_map_init(NULL, 8, 4, 8));
    return 0;
}

static int test_init_zero_dim(void) {
    srd_room_map_t map;
    ASSERT_INT_EQ(-1, srd_room_map_init(&map, 0, 4, 8));
    ASSERT_INT_EQ(-1, srd_room_map_init(&map, 8, 0, 8));
    ASSERT_INT_EQ(-1, srd_room_map_init(&map, 8, 4, 0));
    return 0;
}

static int test_init_ids_zero(void) {
    /* After init, all voxels should have room_id = 0 (wall/void) */
    srd_room_map_t map;
    srd_room_map_init(&map, 4, 4, 4);
    for (int z = 0; z < 4; z++)
        for (int y = 0; y < 4; y++)
            for (int x = 0; x < 4; x++)
                ASSERT_INT_EQ(0, srd_room_map_get(&map, x, y, z));
    srd_room_map_destroy(&map);
    return 0;
}

static int test_destroy_null(void) {
    srd_room_map_destroy(NULL);
    return 0;
}

/* ── Test: get and set ─────────────────────────────────────────── */

static int test_get_set(void) {
    srd_room_map_t map;
    srd_room_map_init(&map, 8, 4, 8);

    srd_room_map_set(&map, 3, 2, 5, 1);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 3, 2, 5));

    srd_room_map_set(&map, 7, 3, 7, 255);
    ASSERT_INT_EQ(255, srd_room_map_get(&map, 7, 3, 7));

    /* Neighbours unchanged */
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 3, 2, 4));

    srd_room_map_destroy(&map);
    return 0;
}

static int test_get_out_of_bounds(void) {
    srd_room_map_t map;
    srd_room_map_init(&map, 4, 4, 4);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, -1, 0, 0));
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 4, 0, 0));
    ASSERT_INT_EQ(0, srd_room_map_get(NULL, 0, 0, 0));
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: stamp_room_box ──────────────────────────────────────── */

static int test_stamp_room_box(void) {
    /* Stamp a room into the map using world-space box coordinates.
     * Grid: 16x8x16 at 0.25m voxels, origin (0,0,0).
     * Room 1: center (2, 1, 2), half (1, 0.5, 1).
     * Voxels inside the box should get room_id = 1. */
    srd_room_map_t map;
    srd_room_map_init(&map, 16, 8, 16);

    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 16, 8, 16, 0.25f, origin);

    /* Carve room into SDF grid first */
    srd_sdf_grid_stamp_box(&grid, 2.0f, 1.0f, 2.0f, 1.0f, 0.5f, 1.0f);

    /* Stamp room identity: assigns room_id to voxels where SDF < 0 */
    uint8_t room_id = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    ASSERT_TRUE(room_id > 0);
    ASSERT_INT_EQ(1, map.n_rooms);

    srd_room_map_stamp_from_sdf(&map, &grid, room_id);

    /* Center voxel (8, 4, 8) should be room 1 */
    ASSERT_INT_EQ(room_id, srd_room_map_get(&map, 8, 4, 8));

    /* Voxel inside: (5, 4, 8) → world (1.25, 1, 2) — inside room */
    ASSERT_INT_EQ(room_id, srd_room_map_get(&map, 5, 4, 8));

    /* Corner voxel (0, 0, 0) → world (0, 0, 0) — wall/void */
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 0, 0, 0));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_stamp_two_rooms(void) {
    /* Stamp two separate rooms and verify they have distinct IDs */
    srd_room_map_t map;
    srd_room_map_init(&map, 32, 8, 32);

    srd_sdf_grid_t grid;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    srd_sdf_grid_init(&grid, 32, 8, 32, 0.25f, origin);

    /* Room 1: center (2, 1, 4), half (1, 0.5, 1) */
    srd_sdf_grid_stamp_box(&grid, 2.0f, 1.0f, 4.0f, 1.0f, 0.5f, 1.0f);
    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);

    /* Room 2: center (6, 1, 4), half (1, 0.5, 1) — separate */
    srd_sdf_grid_stamp_box(&grid, 6.0f, 1.0f, 4.0f, 1.0f, 0.5f, 1.0f);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_CORRIDOR);

    ASSERT_INT_EQ(2, map.n_rooms);
    ASSERT_TRUE(r1 != r2);

    /* Stamp room 1 first (only voxels with SDF < 0 AND current id == 0) */
    /* We need a separate grid per room for stamping, or stamp selectively.
     * Simpler: stamp from the SDF before the second box is added.
     * Let's rebuild: */
    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);

    /* Rebuild properly: stamp room 1, then room 2 */
    srd_room_map_init(&map, 32, 8, 32);
    srd_sdf_grid_init(&grid, 32, 8, 32, 0.25f, origin);

    srd_sdf_grid_stamp_box(&grid, 2.0f, 1.0f, 4.0f, 1.0f, 0.5f, 1.0f);
    r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(&map, &grid, r1);

    srd_sdf_grid_stamp_box(&grid, 6.0f, 1.0f, 4.0f, 1.0f, 0.5f, 1.0f);
    r2 = srd_room_map_add_room(&map, SRD_ROOM_CORRIDOR);
    srd_room_map_stamp_from_sdf(&map, &grid, r2);

    /* Center of room 1: voxel (8, 4, 16) → room r1 */
    ASSERT_INT_EQ(r1, srd_room_map_get(&map, 8, 4, 16));
    /* Center of room 2: voxel (24, 4, 16) → room r2 */
    ASSERT_INT_EQ(r2, srd_room_map_get(&map, 24, 4, 16));
    /* Between rooms: voxel (16, 4, 16) → wall (0) */
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 16, 4, 16));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: room type ───────────────────────────────────────────── */

static int test_room_type(void) {
    srd_room_map_t map;
    srd_room_map_init(&map, 4, 4, 4);

    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_CORRIDOR);
    uint8_t r3 = srd_room_map_add_room(&map, SRD_ROOM_ENTRANCE);

    ASSERT_INT_EQ(SRD_ROOM_GENERIC,  srd_room_map_get_type(&map, r1));
    ASSERT_INT_EQ(SRD_ROOM_CORRIDOR, srd_room_map_get_type(&map, r2));
    ASSERT_INT_EQ(SRD_ROOM_ENTRANCE, srd_room_map_get_type(&map, r3));

    srd_room_map_set_type(&map, r2, SRD_ROOM_BOSS);
    ASSERT_INT_EQ(SRD_ROOM_BOSS, srd_room_map_get_type(&map, r2));

    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: compute_adjacency ───────────────────────────────────── */

static int test_adjacency_touching(void) {
    /* Two rooms sharing a wall should be adjacent.
     * Room 1 at voxels x=[2,5], Room 2 at voxels x=[6,9].
     * They share a face at x=5/6. */
    srd_room_map_t map;
    srd_room_map_init(&map, 16, 4, 4);

    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);

    /* Manually set room IDs for a controlled test */
    for (int x = 2; x <= 5; x++)
        for (int y = 1; y <= 2; y++)
            for (int z = 1; z <= 2; z++)
                srd_room_map_set(&map, x, y, z, r1);

    for (int x = 6; x <= 9; x++)
        for (int y = 1; y <= 2; y++)
            for (int z = 1; z <= 2; z++)
                srd_room_map_set(&map, x, y, z, r2);

    srd_room_map_compute_adjacency(&map);

    ASSERT_TRUE(srd_room_map_are_adjacent(&map, r1, r2));
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, r2, r1)); /* symmetric */

    srd_room_map_destroy(&map);
    return 0;
}

static int test_adjacency_separated(void) {
    /* Two rooms with a gap should NOT be adjacent. */
    srd_room_map_t map;
    srd_room_map_init(&map, 16, 4, 4);

    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);

    for (int x = 1; x <= 3; x++)
        for (int y = 1; y <= 2; y++)
            srd_room_map_set(&map, x, y, 1, r1);

    /* Gap at x=4,5 */
    for (int x = 6; x <= 8; x++)
        for (int y = 1; y <= 2; y++)
            srd_room_map_set(&map, x, y, 1, r2);

    srd_room_map_compute_adjacency(&map);

    ASSERT_TRUE(!srd_room_map_are_adjacent(&map, r1, r2));

    srd_room_map_destroy(&map);
    return 0;
}

static int test_adjacency_three_rooms(void) {
    /* Room 1 touches Room 2, Room 2 touches Room 3, but 1 doesn't touch 3. */
    srd_room_map_t map;
    srd_room_map_init(&map, 16, 4, 4);

    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_CORRIDOR);
    uint8_t r3 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);

    for (int x = 0; x <= 3; x++)
        srd_room_map_set(&map, x, 1, 1, r1);
    for (int x = 4; x <= 7; x++)
        srd_room_map_set(&map, x, 1, 1, r2);
    for (int x = 8; x <= 11; x++)
        srd_room_map_set(&map, x, 1, 1, r3);

    srd_room_map_compute_adjacency(&map);

    ASSERT_TRUE(srd_room_map_are_adjacent(&map, r1, r2));
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, r2, r3));
    ASSERT_TRUE(!srd_room_map_are_adjacent(&map, r1, r3));

    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: count_room_volume ───────────────────────────────────── */

static int test_room_volume(void) {
    srd_room_map_t map;
    srd_room_map_init(&map, 8, 4, 8);

    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);

    /* Set 12 voxels to room 1 */
    for (int x = 0; x < 3; x++)
        for (int y = 0; y < 2; y++)
            for (int z = 0; z < 2; z++)
                srd_room_map_set(&map, x, y, z, r1);

    ASSERT_INT_EQ(12, srd_room_map_count_volume(&map, r1));
    ASSERT_INT_EQ(0, srd_room_map_count_volume(&map, 2)); /* non-existent room */

    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: max rooms ───────────────────────────────────────────── */

static int test_max_rooms(void) {
    srd_room_map_t map;
    srd_room_map_init(&map, 4, 4, 4);

    /* Should be able to add at least SRD_ROOM_MAP_MAX_ROOMS rooms */
    for (int i = 0; i < SRD_ROOM_MAP_MAX_ROOMS; i++) {
        uint8_t id = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
        ASSERT_TRUE(id > 0);
    }
    ASSERT_INT_EQ(SRD_ROOM_MAP_MAX_ROOMS, map.n_rooms);

    /* Adding one more should fail */
    uint8_t overflow = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    ASSERT_INT_EQ(0, overflow);

    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: set_adjacency (manual) ──────────────────────────────── */

static int test_set_adjacency(void) {
    srd_room_map_t map;
    srd_room_map_init(&map, 4, 4, 4);

    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_GENERIC);

    /* Not adjacent initially */
    ASSERT_TRUE(!srd_room_map_are_adjacent(&map, r1, r2));

    /* Set adjacent manually (e.g., from seed layout connectivity) */
    srd_room_map_set_adjacent(&map, r1, r2, true);
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, r1, r2));
    ASSERT_TRUE(srd_room_map_are_adjacent(&map, r2, r1));

    /* Clear */
    srd_room_map_set_adjacent(&map, r1, r2, false);
    ASSERT_TRUE(!srd_room_map_are_adjacent(&map, r1, r2));

    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"init_basic",           test_init_basic},
    {"init_null",            test_init_null},
    {"init_zero_dim",        test_init_zero_dim},
    {"init_ids_zero",        test_init_ids_zero},
    {"destroy_null",         test_destroy_null},
    {"get_set",              test_get_set},
    {"get_out_of_bounds",    test_get_out_of_bounds},
    {"stamp_room_box",       test_stamp_room_box},
    {"stamp_two_rooms",      test_stamp_two_rooms},
    {"room_type",            test_room_type},
    {"adjacency_touching",   test_adjacency_touching},
    {"adjacency_separated",  test_adjacency_separated},
    {"adjacency_three_rooms", test_adjacency_three_rooms},
    {"room_volume",          test_room_volume},
    {"max_rooms",            test_max_rooms},
    {"set_adjacency",        test_set_adjacency},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;

    fprintf(stderr, "srd_room_map_tests: %zu tests\n", total);
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
