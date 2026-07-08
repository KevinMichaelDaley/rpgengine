/**
 * @file srd_rules_vfeature_tests.c
 * @brief Tests for voxel feature rules: pillar, convert type.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_rules_vfeature.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_room_type.h"
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
    if (srd_sdf_grid_init(grid, 16, 12, 16, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 16, 12, 16) != 0) return -1;
    srd_sdf_grid_stamp_box(grid, 4.0f, 3.0f, 4.0f, 2.0f, 1.5f, 2.0f);
    uint8_t rid = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, rid);
    return 0;
}

/* ── Test: add pillar ─────────────────────────────────────────── */

static int test_add_pillar(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = -1,
        .param = 1.5f  /* pillar radius = 1.5 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_add_pillar(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Pillar makes voxels solid → room smaller */
    ASSERT_TRUE(vol_after < vol_before);

    /* Center of room (8, 6, 8) in voxels should now be solid */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 6, 8) >= 0.0f);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 8, 6, 8));

    /* Voxels far from center should still be room */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 5, 6, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 5, 6, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: remove pillar (inverse) ────────────────────────────── */

static int test_add_remove_roundtrip(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_original = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = -1,
        .param = 1.5f
    };

    ASSERT_INT_EQ(0, srd_rule_add_pillar(&grid, &map, &sel));

    int vol_with_pillar = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_with_pillar < vol_original);

    ASSERT_INT_EQ(0, srd_rule_remove_pillar(&grid, &map, &sel));

    int vol_restored = srd_room_map_count_volume(&map, 1);
    ASSERT_INT_EQ(vol_original, vol_restored);

    /* Center should be room again */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 6, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 8, 6, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: convert type ───────────────────────────────────────── */

static int test_convert_type(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    /* Room starts as GENERIC (0) */
    ASSERT_INT_EQ(SRD_ROOM_GENERIC, srd_room_map_get_type(&map, 1));

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = -1,
        .param = 1.0f  /* advance type by 1 */
    };

    ASSERT_INT_EQ(0, srd_rule_convert_type(&grid, &map, &sel));

    /* Should now be BAR (1) */
    ASSERT_INT_EQ(SRD_ROOM_BAR, srd_room_map_get_type(&map, 1));

    /* Advance by 2 more */
    sel.param = 2.0f;
    ASSERT_INT_EQ(0, srd_rule_convert_type(&grid, &map, &sel));
    ASSERT_INT_EQ(SRD_ROOM_PRIVATE, srd_room_map_get_type(&map, 1));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: convert type wraps around ──────────────────────────── */

static int test_convert_type_wrap(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    /* Set to TREASURE (10), then advance by 1 → wraps to GENERIC (0) */
    srd_room_map_set_type(&map, 1, SRD_ROOM_TREASURE);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_NONE, .corner = -1, .param = 1.0f
    };
    ASSERT_INT_EQ(0, srd_rule_convert_type(&grid, &map, &sel));

    ASSERT_INT_EQ(SRD_ROOM_GENERIC, srd_room_map_get_type(&map, 1));

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
        .room_id = 1, .face = SRD_FACE_NONE, .corner = -1, .param = 1.0f
    };

    ASSERT_INT_EQ(-1, srd_rule_add_pillar(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_add_pillar(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_add_pillar(&grid, &map, NULL));

    ASSERT_INT_EQ(-1, srd_rule_remove_pillar(NULL, &map, &sel));

    ASSERT_INT_EQ(-1, srd_rule_convert_type(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_convert_type(&grid, NULL, &sel));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"add_pillar",           test_add_pillar},
    {"add_remove_roundtrip", test_add_remove_roundtrip},
    {"convert_type",         test_convert_type},
    {"convert_type_wrap",    test_convert_type_wrap},
    {"null_inputs",          test_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_rules_vfeature_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
