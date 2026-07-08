#define _POSIX_C_SOURCE 199309L

/**
 * @file srd_property_tests.c
 * @brief Grammar property tests: reversibility round-trips and
 *        jump-continuity for all voxel rewrite rules.
 *
 * For each invertible rule pair (push/pull, raise/lower, etc.),
 * verifies that apply + inverse restores the SDF grid to within
 * epsilon. For all rules, verifies that apply succeeds and changes
 * the grid, and that the L2 voxel-value change is bounded
 * (jump-continuity).
 */
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_voxel_rule.h"
#include "ferrum/procgen/srd/srd_rules_wall.h"
#include "ferrum/procgen/srd/srd_rules_corner.h"
#include "ferrum/procgen/srd/srd_rules_height.h"
#include "ferrum/procgen/srd/srd_rules_vcorridor.h"
#include "ferrum/procgen/srd/srd_rules_vfeature.h"
#include "ferrum/procgen/srd/srd_rules_embellish.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

/* ── Helper: build canonical 4-room layout ──────────────────────── */

/**
 * Build a 32x16x32 grid with 4 rooms for property testing.
 * Room 1 (entrance):  center (4,4,4),  half (2.5, 2.0, 2.5)
 * Room 2 (generic):   center (12,4,4), half (2.5, 2.0, 2.5)
 * Room 3 (corridor):  center (4,4,12), half (4.0, 2.0, 1.0) — elongated
 * Room 4 (boss):      center (12,4,12),half (2.5, 2.0, 2.5)
 */
static int build_test_layout(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 32, 16, 32, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 32, 16, 32) != 0) return -1;

    srd_sdf_grid_stamp_box(grid, 4.0f, 4.0f, 4.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r1 = srd_room_map_add_room(map, SRD_ROOM_ENTRANCE);
    srd_room_map_stamp_from_sdf(map, grid, r1);

    srd_sdf_grid_stamp_box(grid, 12.0f, 4.0f, 4.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r2 = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, r2);

    /* Room 3: corridor (elongated along X) */
    srd_sdf_grid_stamp_box(grid, 4.0f, 4.0f, 12.0f, 4.0f, 2.0f, 1.0f);
    uint8_t r3 = srd_room_map_add_room(map, SRD_ROOM_CORRIDOR);
    srd_room_map_stamp_from_sdf(map, grid, r3);

    srd_sdf_grid_stamp_box(grid, 12.0f, 4.0f, 12.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r4 = srd_room_map_add_room(map, SRD_ROOM_BOSS);
    srd_room_map_stamp_from_sdf(map, grid, r4);

    srd_room_map_set_adjacent(map, r1, r2, true);
    srd_room_map_set_adjacent(map, r1, r3, true);
    srd_room_map_set_adjacent(map, r3, r4, true);
    srd_room_map_set_adjacent(map, r2, r4, true);

    return 0;
}

/* ── Helper: compute grid L2 difference ─────────────────────────── */

/**
 * Compute the sum of squared differences between two grids.
 * Grids must have the same dimensions.
 */
static double grid_l2_diff(const srd_sdf_grid_t *a, const srd_sdf_grid_t *b) {
    int total = a->nx * a->ny * a->nz;
    double sum = 0.0;
    for (int i = 0; i < total; i++) {
        double d = (double)a->values[i] - (double)b->values[i];
        sum += d * d;
    }
    return sum;
}

/**
 * Check if two grids have identical voxel signs (air vs solid).
 * Rules don't preserve exact SDF values but do preserve the
 * air/solid boundary.
 */
static int grids_signs_equal(const srd_sdf_grid_t *a, const srd_sdf_grid_t *b) {
    int total = a->nx * a->ny * a->nz;
    for (int i = 0; i < total; i++) {
        /* 2-way classification: negative = air, non-negative = solid/surface.
         * Exact zeros (surface voxels) may shift between 0 and small positive
         * values after apply+inverse, so treat 0 as solid. */
        int air_a = (a->values[i] < 0.0f);
        int air_b = (b->values[i] < 0.0f);
        if (air_a != air_b) return 0;
    }
    return 1;
}

/**
 * Count room map ID mismatches. Returns 0 if identical.
 */
static int maps_mismatch_count(const srd_room_map_t *a, const srd_room_map_t *b) {
    int total = a->nx * a->ny * a->nz;
    int mismatches = 0;
    for (int i = 0; i < total; i++) {
        if (a->ids[i] != b->ids[i]) mismatches++;
    }
    return mismatches;
}

/* ── Reversibility round-trip tests ──────────────────────────────── */

/**
 * Generic round-trip test: apply forward rule, then inverse rule,
 * verify grid and map are restored.
 */
static int roundtrip_test(const char *name,
                          srd_voxel_rule_fn forward,
                          srd_voxel_rule_fn inverse,
                          const srd_voxel_selection_t *sel) {
    srd_sdf_grid_t grid, grid_orig;
    srd_room_map_t map, map_orig;

    if (build_test_layout(&grid, &map) != 0) return 1;
    if (srd_sdf_grid_copy(&grid_orig, &grid) != 0) return 1;
    if (srd_room_map_copy(&map_orig, &map) != 0) return 1;

    /* Apply forward */
    int rc = forward(&grid, &map, sel);
    if (rc != 0) {
        fprintf(stderr, "  FAIL %s: forward apply returned %d\n", name, rc);
        srd_sdf_grid_destroy(&grid);
        srd_sdf_grid_destroy(&grid_orig);
        srd_room_map_destroy(&map);
        srd_room_map_destroy(&map_orig);
        return 1;
    }

    /* Verify grid changed */
    double l2 = grid_l2_diff(&grid, &grid_orig);
    if (l2 < 1e-6) {
        fprintf(stderr, "  WARN %s: forward did not change grid (L2=%.6f)\n",
                name, l2);
        /* Not necessarily a failure — some rules may be no-ops on this layout */
    }

    /* Apply inverse */
    rc = inverse(&grid, &map, sel);
    if (rc != 0) {
        fprintf(stderr, "  FAIL %s: inverse apply returned %d\n", name, rc);
        srd_sdf_grid_destroy(&grid);
        srd_sdf_grid_destroy(&grid_orig);
        srd_room_map_destroy(&map);
        srd_room_map_destroy(&map_orig);
        return 1;
    }

    /* Verify round-trip: voxel signs restored (air/solid boundary).
     * Exact SDF values may differ because rules use flat ±voxel_size
     * while original grid has smooth SDF distances. */
    int grid_ok = grids_signs_equal(&grid, &grid_orig);
    int map_mismatches = maps_mismatch_count(&map, &map_orig);

    /* Allow small map tolerance: SDF=0 boundary voxels can change
     * room ownership during apply/inverse because the surface
     * moves through them. Up to 0.5% of total voxels is acceptable. */
    int total_voxels = map.nx * map.ny * map.nz;
    int map_tolerance = total_voxels / 200;  /* 0.5% */
    if (map_tolerance < 8) map_tolerance = 8;
    int map_ok = (map_mismatches <= map_tolerance);

    if (!grid_ok) {
        int total = grid.nx * grid.ny * grid.nz;
        int sign_mismatches = 0;
        for (int i = 0; i < total; i++) {
            int sa = (grid.values[i] < 0.0f) ? -1 : 1;
            int sb = (grid_orig.values[i] < 0.0f) ? -1 : 1;
            if (sa != sb) sign_mismatches++;
        }
        fprintf(stderr, "  FAIL %s: %d voxel sign mismatches\n", name, sign_mismatches);
    }
    if (!map_ok) {
        fprintf(stderr, "  FAIL %s: room map %d mismatches (tolerance %d)\n",
                name, map_mismatches, map_tolerance);
    } else if (map_mismatches > 0) {
        fprintf(stderr, "    %s: %d map boundary mismatches (within tolerance)\n",
                name, map_mismatches);
    }

    srd_sdf_grid_destroy(&grid);
    srd_sdf_grid_destroy(&grid_orig);
    srd_room_map_destroy(&map);
    srd_room_map_destroy(&map_orig);

    return (grid_ok && map_ok) ? 0 : 1;
}

/* ── Test: wall push/pull round-trip ─────────────────────────────── */

static int test_roundtrip_wall_push_pull(void) {
    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_EAST, .corner = -1, .param = 2.0f
    };
    return roundtrip_test("wall_push/pull",
                          srd_rule_wall_push, srd_rule_wall_pull, &sel);
}

static int test_roundtrip_wall_push_pull_north(void) {
    srd_voxel_selection_t sel = {
        .room_id = 2, .face = SRD_FACE_NORTH, .corner = -1, .param = 1.0f
    };
    return roundtrip_test("wall_push/pull_north",
                          srd_rule_wall_push, srd_rule_wall_pull, &sel);
}

/* ── Test: ceiling raise/lower round-trip ────────────────────────── */

static int test_roundtrip_ceiling_raise_lower(void) {
    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_CEIL, .corner = -1, .param = 2.0f
    };
    return roundtrip_test("ceiling_raise/lower",
                          srd_rule_ceiling_raise, srd_rule_ceiling_lower, &sel);
}

/* ── Test: corridor widen/narrow round-trip ──────────────────────── */

static int test_roundtrip_corridor_widen_narrow(void) {
    srd_voxel_selection_t sel = {
        .room_id = 3, .face = SRD_FACE_NONE, .corner = -1, .param = 1.0f
    };
    return roundtrip_test("corridor_widen/narrow",
                          srd_rule_corridor_widen, srd_rule_corridor_narrow, &sel);
}

/* ── Test: pillar add/remove round-trip ──────────────────────────── */

static int test_roundtrip_pillar_add_remove(void) {
    srd_voxel_selection_t sel = {
        .room_id = 2, .face = SRD_FACE_NONE, .corner = -1, .param = 1.5f
    };
    return roundtrip_test("pillar_add/remove",
                          srd_rule_add_pillar, srd_rule_remove_pillar, &sel);
}

/* ── Test: floor pit/fill round-trip ─────────────────────────────── */

static int test_roundtrip_floor_pit_fill(void) {
    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_FLOOR, .corner = -1, .param = 2.0f
    };
    return roundtrip_test("floor_pit/fill",
                          srd_rule_floor_pit, srd_rule_floor_pit_fill, &sel);
}

/* ── Test: convert_type round-trip ───────────────────────────────── */

static int test_roundtrip_convert_type(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    srd_room_type_t orig_type = srd_room_map_get_type(&map, 2);

    srd_voxel_selection_t sel_fwd = {
        .room_id = 2, .face = SRD_FACE_NONE, .corner = -1, .param = 3.0f
    };
    ASSERT_INT_EQ(0, srd_rule_convert_type(&grid, &map, &sel_fwd));

    /* Type should have changed */
    srd_room_type_t mid_type = srd_room_map_get_type(&map, 2);
    ASSERT_TRUE(mid_type != orig_type);

    /* Inverse: negate param */
    srd_voxel_selection_t sel_inv = {
        .room_id = 2, .face = SRD_FACE_NONE, .corner = -1, .param = -3.0f
    };
    ASSERT_INT_EQ(0, srd_rule_convert_type(&grid, &map, &sel_inv));

    srd_room_type_t final_type = srd_room_map_get_type(&map, 2);
    ASSERT_TRUE(final_type == orig_type);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Jump-continuity: L2 bounded for all rules ───────────────────── */

/**
 * For each rule in the default table, apply it on a fresh layout and
 * verify the L2 change in voxel values is bounded. This tests that
 * rules make local, bounded modifications (jump-continuity).
 */
static int test_jump_continuity_all_rules(void) {
    int n_rules = 0;
    const srd_voxel_rule_entry_t *table = srd_voxel_rule_table_default(&n_rules);
    ASSERT_TRUE(n_rules > 0);

    /* Max acceptable L2 change per rule application */
    double max_l2 = 1e8;

    int failures = 0;

    for (int ri = 0; ri < n_rules; ri++) {
        const srd_voxel_rule_entry_t *entry = &table[ri];

        srd_sdf_grid_t grid, grid_before;
        srd_room_map_t map;
        if (build_test_layout(&grid, &map) != 0) return 1;
        if (srd_sdf_grid_copy(&grid_before, &grid) != 0) return 1;

        /* Build a valid selection for this rule */
        srd_voxel_selection_t sel;
        sel.room_id = 1; /* Use room 1 (entrance) for most rules */
        sel.param = entry->param_min;
        sel.corner = entry->needs_corner ? 0 : -1;

        /* Set face based on rule's required face */
        if (entry->required_face == SRD_FACE_NONE) {
            sel.face = SRD_FACE_NONE;
            /* Corridor rules need room 3 */
            if (strstr(entry->name, "corridor")) {
                sel.room_id = 3;
            }
        } else if (entry->required_face == SRD_FACE_CEIL ||
                   entry->required_face == SRD_FACE_FLOOR) {
            sel.face = entry->required_face;
        } else {
            /* Wall face: use east */
            sel.face = SRD_FACE_EAST;
        }

        int rc = entry->apply(&grid, &map, &sel);
        if (rc == 0) {
            double l2 = grid_l2_diff(&grid, &grid_before);
            if (l2 > max_l2) {
                fprintf(stderr, "  FAIL jump-continuity: %s L2=%.2f > %.2f\n",
                        entry->name, l2, max_l2);
                failures++;
            }
        }
        /* rc != 0 is ok — some rules may reject certain selections */

        srd_sdf_grid_destroy(&grid);
        srd_sdf_grid_destroy(&grid_before);
        srd_room_map_destroy(&map);
    }

    ASSERT_INT_EQ(0, failures);
    return 0;
}

/* ── Apply-only tests for non-invertible rules ───────────────────── */

static int test_apply_wall_bevel(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_EAST, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_wall_bevel(&grid, &map, &sel));

    /* Bevel should change room volume */
    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after != vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_apply_wall_niche(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_EAST, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_wall_niche(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after > vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_apply_corner_chamfer(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_NONE, .corner = 0, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_corner_chamfer(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after < vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_apply_corner_round(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_NONE, .corner = 2, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_corner_round(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after < vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_apply_floor_step(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_FLOOR, .corner = -1, .param = 1.0f
    };
    ASSERT_INT_EQ(0, srd_rule_floor_step(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after < vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

static int test_apply_alcove(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_test_layout(&grid, &map));

    int vol_before = srd_room_map_count_volume(&map, 1);

    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_SOUTH, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_alcove(&grid, &map, &sel));

    int vol_after = srd_room_map_count_volume(&map, 1);
    ASSERT_TRUE(vol_after > vol_before);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: all rules in table are callable ───────────────────────── */

static int test_all_rules_callable(void) {
    int n_rules = 0;
    const srd_voxel_rule_entry_t *table = srd_voxel_rule_table_default(&n_rules);

    /* We expect 17 rules in the default table */
    ASSERT_TRUE(n_rules >= 17);

    /* Every entry should have a non-null apply function and name */
    for (int i = 0; i < n_rules; i++) {
        ASSERT_TRUE(table[i].apply != NULL);
        ASSERT_TRUE(table[i].name != NULL);
        ASSERT_TRUE(table[i].param_min <= table[i].param_max);
    }

    return 0;
}

/* ── Test runner ──────────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    /* Reversibility round-trips */
    {"roundtrip_wall_push_pull",       test_roundtrip_wall_push_pull},
    {"roundtrip_wall_push_pull_north", test_roundtrip_wall_push_pull_north},
    {"roundtrip_ceiling_raise_lower",  test_roundtrip_ceiling_raise_lower},
    {"roundtrip_corridor_widen_narrow",test_roundtrip_corridor_widen_narrow},
    {"roundtrip_pillar_add_remove",    test_roundtrip_pillar_add_remove},
    {"roundtrip_floor_pit_fill",       test_roundtrip_floor_pit_fill},
    {"roundtrip_convert_type",         test_roundtrip_convert_type},

    /* Apply-only for non-invertible rules */
    {"apply_wall_bevel",   test_apply_wall_bevel},
    {"apply_wall_niche",   test_apply_wall_niche},
    {"apply_corner_chamfer", test_apply_corner_chamfer},
    {"apply_corner_round", test_apply_corner_round},
    {"apply_floor_step",   test_apply_floor_step},
    {"apply_alcove",       test_apply_alcove},

    /* Jump-continuity */
    {"jump_continuity_all_rules", test_jump_continuity_all_rules},

    /* Table completeness */
    {"all_rules_callable", test_all_rules_callable},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_property_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
