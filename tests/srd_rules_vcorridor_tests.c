/**
 * @file srd_rules_vcorridor_tests.c
 * @brief Tests for voxel corridor widen/narrow rewrite rules.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_rules_vcorridor.h"
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

/* ── Helper: build a corridor (long in X, narrow in Z) ────────── */

/**
 * Build a 32x12x16 grid with a corridor room that is long in X, narrow in Z.
 * Corridor: center (8, 3, 4), half (6, 1.5, 1) → X=[2,14], Z=[3,5].
 * Width in Z = 4 voxels, length in X = 24 voxels.
 */
static int build_corridor(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 32, 12, 16, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 32, 12, 16) != 0) return -1;
    srd_sdf_grid_stamp_box(grid, 8.0f, 3.0f, 4.0f, 6.0f, 1.5f, 1.0f);
    uint8_t rid = srd_room_map_add_room(map, SRD_ROOM_CORRIDOR);
    srd_room_map_stamp_from_sdf(map, grid, rid);
    return 0;
}

/* ── Test: widen corridor ─────────────────────────────────────── */

static int test_widen(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_corridor(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = -1,
        .param = 2.0f  /* widen by 2 voxels per side */
    };

    ASSERT_INT_EQ(0, srd_rule_corridor_widen(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Corridor should be wider → more voxels */
    ASSERT_TRUE(vol_after > vol_before);

    /* Voxels just outside old Z boundary should now be corridor.
     * Old Z=[6,10) in voxels. After widen by 2, Z=[4,12).
     * Voxel (16, 6, 5) was solid, should now be room. */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 16, 6, 5) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 16, 6, 5));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: narrow corridor ────────────────────────────────────── */

static int test_narrow(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_corridor(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NONE,
        .corner = -1,
        .param = 1.0f  /* narrow by 1 voxel per side */
    };

    ASSERT_INT_EQ(0, srd_rule_corridor_narrow(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);

    /* Corridor should be narrower → fewer voxels */
    ASSERT_TRUE(vol_after < vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: widen then narrow round-trip ───────────────────────── */

static int test_widen_narrow_roundtrip(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_corridor(&grid, &map));

    int vol_original = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t widen_sel = {
        .room_id = 1, .face = SRD_FACE_NONE, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_corridor_widen(&grid, &map, &widen_sel));

    srd_voxel_selection_t narrow_sel = {
        .room_id = 1, .face = SRD_FACE_NONE, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_corridor_narrow(&grid, &map, &narrow_sel));

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
        .room_id = 1, .face = SRD_FACE_NONE, .corner = -1, .param = 1.0f
    };

    ASSERT_INT_EQ(-1, srd_rule_corridor_widen(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_corridor_widen(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_corridor_widen(&grid, &map, NULL));

    ASSERT_INT_EQ(-1, srd_rule_corridor_narrow(NULL, &map, &sel));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"widen",                  test_widen},
    {"narrow",                 test_narrow},
    {"widen_narrow_roundtrip", test_widen_narrow_roundtrip},
    {"null_inputs",            test_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_rules_vcorridor_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
