/**
 * @file srd_rules_wall_tests.c
 * @brief Tests for wall rewrite rules: push, pull, bevel, niche.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_rules_wall.h"
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

/**
 * Build a 16x12x16 grid (voxel_size=0.5, origin=0,0,0) with one room.
 * Room 1: center (4, 3, 4), half (2, 1.5, 2).
 *   → voxel extent X=[4,12), Y=[3,9), Z=[4,12) (in voxel coords).
 *   → world extent X=[2,6], Y=[1.5,4.5], Z=[2,6].
 */
static int build_one_room(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 16, 12, 16, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 16, 12, 16) != 0) return -1;

    /* Stamp room as box SDF */
    srd_sdf_grid_stamp_box(grid, 4.0f, 3.0f, 4.0f, 2.0f, 1.5f, 2.0f);

    /* Assign room 1 */
    uint8_t rid = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, rid);

    return 0;
}

/**
 * Count how many voxels belong to a room in the room map.
 */
static int count_room_voxels(const srd_room_map_t *map, uint8_t room_id) {
    return srd_room_map_count_volume(map, room_id);
}

/* ── Test: wall push shrinks the room ─────────────────────────── */

static int test_wall_push_east(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = count_room_voxels(&map, 1);
    ASSERT_TRUE(vol_before > 0);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f  /* push east wall inward by 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_wall_push(&grid, &map, &sel));

    int vol_after = count_room_voxels(&map, 1);

    /* Room should be smaller after pushing wall inward */
    ASSERT_TRUE(vol_after < vol_before);

    /* The eastern-most voxels that were room should now be solid */
    /* Room was X=[4,12) in voxels. After push by 2, X=[4,10). */
    /* Voxel (11, 6, 8) was inside room, should now be solid */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 11, 6, 8) >= 0.0f);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 11, 6, 8));

    /* Voxel (9, 6, 8) should still be inside room */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 9, 6, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 9, 6, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: wall push on other faces ───────────────────────────── */

static int test_wall_push_north(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = count_room_voxels(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_NORTH,  /* -Z */
        .corner = -1,
        .param = 2.0f
    };

    ASSERT_INT_EQ(0, srd_rule_wall_push(&grid, &map, &sel));

    int vol_after = count_room_voxels(&map, 1);
    ASSERT_TRUE(vol_after < vol_before);

    /* North = -Z direction. Push inward means z_min increases.
     * Room was Z=[4,12). After push by 2 on north face, Z=[6,12).
     * Voxel (8, 6, 4) was inside, should now be solid. */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 6, 4) >= 0.0f);
    ASSERT_INT_EQ(0, srd_room_map_get(&map, 8, 6, 4));

    /* Voxel (8, 6, 7) should still be inside */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 8, 6, 7) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 8, 6, 7));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: wall pull expands the room ─────────────────────────── */

static int test_wall_pull_east(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = count_room_voxels(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f  /* pull east wall outward by 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_wall_pull(&grid, &map, &sel));

    int vol_after = count_room_voxels(&map, 1);

    /* Room should be larger after pulling wall outward */
    ASSERT_TRUE(vol_after > vol_before);

    /* Voxels just beyond old east wall should now be room.
     * Room was X=[4,12). After pull by 2, X=[4,14).
     * Voxel (12, 6, 8) was solid, should now be room. */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 12, 6, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 12, 6, 8));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: push then pull round-trip ──────────────────────────── */

static int test_push_pull_roundtrip(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_original = count_room_voxels(&map, 1);

    /* Push east wall in by 2 */
    srd_voxel_selection_t push_sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_wall_push(&grid, &map, &push_sel));

    int vol_pushed = count_room_voxels(&map, 1);
    ASSERT_TRUE(vol_pushed < vol_original);

    /* Pull east wall back out by 2 (inverse) */
    srd_voxel_selection_t pull_sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_wall_pull(&grid, &map, &pull_sel));

    int vol_restored = count_room_voxels(&map, 1);

    /* Volume should be restored after round-trip */
    ASSERT_INT_EQ(vol_original, vol_restored);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: wall bevel carves corner ───────────────────────────── */

static int test_wall_bevel(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = count_room_voxels(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f  /* bevel width = 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_wall_bevel(&grid, &map, &sel));

    int vol_after = count_room_voxels(&map, 1);

    /* Bevel carves into the wall-ceiling corner, room gets bigger */
    ASSERT_TRUE(vol_after > vol_before);

    /* The corner where east wall meets ceiling should now be carved.
     * East wall is at x=12, ceiling is at y=9.
     * A bevel of width 2 should carve voxels near (12, 8, 8). */
    /* Voxel at the corner (12, 8, 8) was solid wall, should now be room */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 12, 8, 8) < 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: wall niche carves alcove ───────────────────────────── */

static int test_wall_niche(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = count_room_voxels(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 2.0f  /* niche depth = 2 voxels */
    };

    ASSERT_INT_EQ(0, srd_rule_wall_niche(&grid, &map, &sel));

    int vol_after = count_room_voxels(&map, 1);

    /* Niche carves into the wall, room gets bigger */
    ASSERT_TRUE(vol_after > vol_before);

    /* The niche should carve voxels just beyond the east wall, centered.
     * East wall is at x=12. Niche extends to x=14.
     * Center of face is roughly y=6, z=8.
     * Voxel (12, 6, 8) was solid, should now be room (inside niche). */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 12, 6, 8) < 0.0f);
    ASSERT_INT_EQ(1, srd_room_map_get(&map, 12, 6, 8));

    /* Voxels outside the niche region should still be solid.
     * The niche is centered, so corner voxels should remain solid. */
    /* Voxel at far corner (12, 3, 4) should still be solid */
    ASSERT_TRUE(srd_sdf_grid_get(&grid, 12, 3, 4) >= 0.0f);

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
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 1.0f
    };

    /* All should return -1 for NULL inputs */
    ASSERT_INT_EQ(-1, srd_rule_wall_push(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_wall_push(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_wall_push(&grid, &map, NULL));

    ASSERT_INT_EQ(-1, srd_rule_wall_pull(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_wall_pull(&grid, NULL, &sel));
    ASSERT_INT_EQ(-1, srd_rule_wall_pull(&grid, &map, NULL));

    ASSERT_INT_EQ(-1, srd_rule_wall_bevel(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_wall_bevel(&grid, NULL, &sel));

    ASSERT_INT_EQ(-1, srd_rule_wall_niche(NULL, &map, &sel));
    ASSERT_INT_EQ(-1, srd_rule_wall_niche(&grid, NULL, &sel));

    /* Invalid face for wall rules */
    srd_voxel_selection_t bad_face = sel;
    bad_face.face = SRD_FACE_CEIL;
    ASSERT_INT_EQ(-1, srd_rule_wall_push(&grid, &map, &bad_face));

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: push with zero param is no-op ──────────────────────── */

static int test_push_zero_param(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_one_room(&grid, &map));

    int vol_before = count_room_voxels(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1,
        .face = SRD_FACE_EAST,
        .corner = -1,
        .param = 0.0f
    };

    ASSERT_INT_EQ(0, srd_rule_wall_push(&grid, &map, &sel));

    int vol_after = count_room_voxels(&map, 1);
    ASSERT_INT_EQ(vol_before, vol_after);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"wall_push_east",     test_wall_push_east},
    {"wall_push_north",    test_wall_push_north},
    {"wall_pull_east",     test_wall_pull_east},
    {"push_pull_roundtrip", test_push_pull_roundtrip},
    {"wall_bevel",         test_wall_bevel},
    {"wall_niche",         test_wall_niche},
    {"null_inputs",        test_null_inputs},
    {"push_zero_param",    test_push_zero_param},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;

    fprintf(stderr, "srd_rules_wall_tests: %zu tests\n", total);
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
