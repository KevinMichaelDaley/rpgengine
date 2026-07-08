/**
 * @file srd_rules_embellish_tests.c
 * @brief Tests for embellishment rules: alcove, floor pit, floor pit fill.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_rules_embellish.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"

#include <stdio.h>

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

/* ── Helper ───────────────────────────────────────────────────── */

static int build_one_room(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 20, 16, 20, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 20, 16, 20) != 0) return -1;
    /* Room: center (5, 4, 5), half (2.5, 2, 2.5) → world [2.5, 7.5] each axis */
    srd_sdf_grid_stamp_box(grid, 5.0f, 4.0f, 5.0f, 2.5f, 2.0f, 2.5f);
    uint8_t rid = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, rid);
    return 0;
}

/* ── Test: alcove on east wall ────────────────────────────────── */

static int test_alcove_east(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f  /* alcove depth = 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_alcove(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Alcove carves into wall → room gets bigger */
    ASSERT_TRUE(vol_after > vol_before);

    /* Center of east wall should be carved.
     * Room center z≈10, y≈8 in voxels. Just past east wall edge. */
    /* The alcove should have carved some voxels beyond the room's east face */

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: alcove on west wall ────────────────────────────────── */

static int test_alcove_west(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_WEST,
        .corner = -1,
        .param = 2.0f
    };

    ASSERT_INT_EQ(0, srd_rule_alcove(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after > vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: floor pit ──────────────────────────────────────────── */

static int test_floor_pit(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_FLOOR,
        .corner = -1,
        .param = 2.0f  /* pit depth = 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_floor_pit(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Pit carves downward → room gets bigger */
    ASSERT_TRUE(vol_after > vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: floor pit then fill round-trip ─────────────────────── */

static int test_pit_fill_roundtrip(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_original = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_FLOOR,
        .corner = -1,
        .param = 2.0f
    };

    ASSERT_INT_EQ(0, srd_rule_floor_pit(&grid, &map, &sel));

    int vol_pit = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_pit > vol_original);

    ASSERT_INT_EQ(0, srd_rule_floor_pit_fill(&grid, &map, &sel));

    int vol_restored = srd_room_map_count_volume(&map, 1);
    ASSERT_INT_EQ(vol_original, vol_restored);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: null inputs ────────────────────────────────────────── */

static int test_null_inputs(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.5f, origin);
    srd_room_map_init(&map, 4, 4, 4);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_EAST, .corner = -1, .param = 1.0f
    };

    ASSERT_INT_EQ(-1, srd_rule_alcove(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_alcove(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_alcove(&grid, &map, NULL));

    srd_voxel_selection_t pit_sel = {
        .room_id = 1, .face = SRD_FACE_FLOOR, .corner = -1, .param = 1.0f
    };
    ASSERT_INT_EQ(-1, srd_rule_floor_pit(NULL, &map, &pit_sel));
    ASSERT_INT_EQ(-1, srd_rule_floor_pit_fill(NULL, &map, &pit_sel));

    /* Wrong face for alcove */
    srd_voxel_selection_t bad = sel;
    bad.face = SRD_FACE_CEIL;
    ASSERT_INT_EQ(-1, srd_rule_alcove(&grid, &map, &bad));

    /* Wrong face for floor pit */
    srd_voxel_selection_t bad2 = pit_sel;
    bad2.face = SRD_FACE_EAST;
    ASSERT_INT_EQ(-1, srd_rule_floor_pit(&grid, &map, &bad2));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"alcove_east",        test_alcove_east},
    {"alcove_west",        test_alcove_west},
    {"floor_pit",          test_floor_pit},
    {"pit_fill_roundtrip", test_pit_fill_roundtrip},
    {"null_inputs",        test_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_rules_embellish_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
