/**
 * @file srd_rules_height_tests.c
 * @brief Tests for ceiling/floor height rewrite rules.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_rules_height.h"
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
    if (srd_sdf_grid_init(grid, 16, 16, 16, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 16, 16, 16) != 0) return -1;
    srd_sdf_grid_stamp_box(grid, 4.0f, 4.0f, 4.0f, 2.0f, 2.0f, 2.0f);
    uint8_t rid = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, rid);
    return 0;
}

/* ── Test: ceiling raise ──────────────────────────────────────── */

static int test_ceiling_raise(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_CEIL,
        .corner = -1,
        .param = 2.0f  /* raise ceiling by 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_ceiling_raise(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Room should be larger (ceiling moved up) */
    ASSERT_TRUE(vol_after > vol_before);

    /* Room was Y=[4,12) in voxels. After raise by 2, Y=[4,14).
     * Voxel (8, 12, 8) was solid, should now be room. */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 12, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 8, 12, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: ceiling lower ──────────────────────────────────────── */

static int test_ceiling_lower(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_CEIL,
        .corner = -1,
        .param = 2.0f  /* lower ceiling by 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_ceiling_lower(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Room should be smaller */
    ASSERT_TRUE(vol_after < vol_before);

    /* Room was Y=[4,12). After lower by 2, Y=[4,10).
     * Voxel (8, 11, 8) was room, should now be solid. */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 11, 8) >= 0.0f);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 8, 11, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: raise then lower round-trip ────────────────────────── */

static int test_ceiling_roundtrip(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_original = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t raise_sel = {
        .room_id = 1, .face = SRD_FACE_CEIL, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_ceiling_raise(&grid, &map, &raise_sel));

    srd_voxel_selection_t lower_sel = {
        .room_id = 1, .face = SRD_FACE_CEIL, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_ceiling_lower(&grid, &map, &lower_sel));

    int vol_restored = srd_room_map_count_volume(&map, 1);
    ASSERT_INT_EQ(vol_original, vol_restored);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: floor step ─────────────────────────────────────────── */

static int test_floor_step(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_FLOOR,
        .corner = -1,
        .param = 2.0f  /* step height = 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_floor_step(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Floor step fills in voxels → room smaller */
    ASSERT_TRUE(vol_after < vol_before);

    /* The center of the room floor should have a raised step.
     * Room Y range at center is [5,11]. Step raises floor by 2 in center.
     * Voxel (8, 5, 8) — was room floor, should now be solid (step). */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 5, 8) >= 0.0f);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 8, 5, 8));

    /* Voxel above the step (y=7) should still be room */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 7, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 8, 7, 8));

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
        .room_id = 1, .face = SRD_FACE_CEIL, .corner = -1, .param = 1.0f
    };

    ASSERT_INT_EQ(-1, srd_rule_ceiling_raise(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_ceiling_raise(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_ceiling_raise(&grid, &map, NULL));

    ASSERT_INT_EQ(-1, srd_rule_ceiling_lower(NULL, &map, &sel));

    srd_voxel_selection_t floor_sel = sel;
    floor_sel.face = SRD_FACE_FLOOR;
    ASSERT_INT_EQ(-1, srd_rule_floor_step(NULL, &map, &floor_sel));

    /* Wrong face for ceiling rule */
    srd_voxel_selection_t bad = sel;
    bad.face = SRD_FACE_EAST;
    ASSERT_INT_EQ(-1, srd_rule_ceiling_raise(&grid, &map, &bad));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"ceiling_raise",     test_ceiling_raise},
    {"ceiling_lower",     test_ceiling_lower},
    {"ceiling_roundtrip", test_ceiling_roundtrip},
    {"floor_step",        test_floor_step},
    {"null_inputs",       test_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_rules_height_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
