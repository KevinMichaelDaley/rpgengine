/**
 * @file srd_rules_corner_tests.c
 * @brief Tests for corner chamfer and round rewrite rules.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_rules_corner.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#include <stdio.h>
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

/* ── Helper: build a single-room grid ─────────────────────────── */

static int build_one_room(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 16, 12, 16, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 16, 12, 16) != 0) return -1;
    srd_sdf_grid_stamp_box(grid, 4.0f, 3.0f, 4.0f, 2.0f, 1.5f, 2.0f);
    uint8_t rid = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, rid);
    return 0;
}

/* ── Test: chamfer NE corner ──────────────────────────────────── */

static int test_chamfer_ne(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = 0,  /* NE = +X, -Z */
        .param = 3.0f
    };

    ASSERT_INT_EQ(0, srd_rule_corner_chamfer(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Chamfer cuts material away from the corner → room gets smaller */
    ASSERT_TRUE(vol_after < vol_before);

    /* The NE corner voxel (x=x1, z=z0) should now be solid.
     * Room X=[4,12), Z=[4,12). NE corner = (11, 6, 4).
     * With chamfer=3, the very corner should be filled. */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 11, 6, 4) >= 0.0f);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 11, 6, 4));

    /* Interior voxel far from corner should still be room */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 6, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 8, 6, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: chamfer SW corner ──────────────────────────────────── */

static int test_chamfer_sw(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = 3,  /* SW = -X, +Z */
        .param = 3.0f
    };

    ASSERT_INT_EQ(0, srd_rule_corner_chamfer(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after < vol_before);

    /* SW corner voxel (x=x0, z=z1) = (4, 6, 11) should be filled */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 4, 6, 11) >= 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: round NE corner ────────────────────────────────────── */

static int test_round_ne(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = 0,  /* NE */
        .param = 3.0f /* radius = 3 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_corner_round(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Round cuts the corner → room smaller */
    ASSERT_TRUE(vol_after < vol_before);

    /* Corner voxel should be filled */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 11, 6, 4) >= 0.0f);

    /* Round should remove fewer voxels than chamfer with same param
     * (circle inscribed in triangle). Build fresh rooms to compare. */
    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);

    srd_sdf_grid_t grid_c, grid_r;
    srd_room_map_t map_c, map_r;
    ASSERT_INT_EQ(0, build_one_room(&grid_c, &map_c));
    ASSERT_INT_EQ(0, build_one_room(&grid_r, &map_r));

    srd_voxel_selection_t sel_c = { .room_id=1, .corner=0, .param=3.0f };
    srd_voxel_selection_t sel_r = { .room_id=1, .corner=0, .param=3.0f };

    srd_rule_corner_chamfer(&grid_c, &map_c, &sel_c);
    srd_rule_corner_round(&grid_r, &map_r, &sel_r);

    int vol_chamfer = srd_room_map_count_volume(&map_c, 1);
    int vol_round = srd_room_map_count_volume(&map_r, 1);

    /* Round removes less than chamfer */
    ASSERT_TRUE(vol_round > vol_chamfer);

    srd_sdf_grid_destroy(&grid_c);
    srd_room_map_destroy(&map_c);
    srd_sdf_grid_destroy(&grid_r);
    srd_room_map_destroy(&map_r);
    return 0;
}

/* ── Test: null inputs ────────────────────────────────────────── */

static int test_null_inputs(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.5f, origin);
    srd_room_map_init(&map, 4, 4, 4);

    srd_voxel_selection_t sel = { .room_id=1, .corner=0, .param=1.0f };

    ASSERT_INT_EQ(-1, srd_rule_corner_chamfer(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_corner_chamfer(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_corner_chamfer(&grid, &map, NULL));

    ASSERT_INT_EQ(-1, srd_rule_corner_round(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_corner_round(&grid, NULL, &sel));

    /* Invalid corner index */
    srd_voxel_selection_t bad = sel;
    bad.corner = 5;
    ASSERT_INT_EQ(-1, srd_rule_corner_chamfer(&grid, &map, &bad));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"chamfer_ne",    test_chamfer_ne},
    {"chamfer_sw",    test_chamfer_sw},
    {"round_ne",      test_round_ne},
    {"null_inputs",   test_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_rules_corner_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
