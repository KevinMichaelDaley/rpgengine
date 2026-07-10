#define _POSIX_C_SOURCE 199309L

/**
 * @file srd_bridge_tests.c
 * @brief Tests for the rewritten SRD bridge (ASCII → SDF → optimize → SVO).
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/npc/npc_svo.h"

#include <stdio.h>
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

/* ── Test ASCII grid ──────────────────────────────────────────── */

/* Simple 2-room layout: E=entrance, G=generic, connected via adjacency */
static const char *SIMPLE_GRID =
    "EEGG\n"
    "EEGG\n";

/* Larger 3-room layout */
static const char *THREE_ROOM_GRID =
    "EEEGG\n"
    "EEEGG\n"
    "EEEGG\n"
    "BBBBB\n";

/* ── Test: srd_generate_svo with simple grid ─────────────────── */

static int test_generate_svo_simple(void) {
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    int rc = srd_generate_svo(SIMPLE_GRID, 42, 0.5, &svo);
    ASSERT_INT_EQ(0, rc);

    /* SVO should have been initialized with some solid voxels */
    ASSERT_TRUE(svo.max_depth > 0);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test: srd_generate_svo with three rooms ─────────────────── */

static int test_generate_svo_three_rooms(void) {
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    int rc = srd_generate_svo(THREE_ROOM_GRID, 13, 1.0, &svo);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(svo.max_depth > 0);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test: null inputs ───────────────────────────────────────── */

static int test_null_inputs(void) {
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    ASSERT_INT_EQ(-1, srd_generate_svo(NULL, 0, 1.0, &svo));
    ASSERT_INT_EQ(-1, srd_generate_svo(SIMPLE_GRID, 0, 1.0, NULL));

    return 0;
}

/* ── Test: empty grid ────────────────────────────────────────── */

static int test_empty_grid(void) {
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    ASSERT_INT_EQ(-1, srd_generate_svo("", 0, 1.0, &svo));

    return 0;
}

/* ── Test: backward-compat srd_generate still works ──────────── */

static int test_legacy_generate(void) {
    fr_room_box_t *rooms = NULL;
    uint32_t nr = 0;
    fr_corridor_seg_t *corrs = NULL;
    uint32_t nc = 0;

    int rc = srd_generate(SIMPLE_GRID, 42, 0.5, &rooms, &nr, &corrs, &nc);
    ASSERT_INT_EQ(0, rc);

    /* Legacy API: rooms may be empty (bridge no longer produces tiles),
     * but it should not crash */

    srd_free_geometry(rooms, corrs);
    return 0;
}

/* ── Test: two-floor dungeon with stairs ─────────────────────── */

static const char *TWO_FLOOR_GRID =
    "=== FLOOR 0: CRYPT ===\n"
    "W W W W W W W W W W W W W W W\n"
    "W E E E W R R R R R R R R R W\n"
    "W E E E . R R R R R R R R R W\n"
    "W E E E W R R R R R R R R R W\n"
    "W W . W W W W W W W . W W W W\n"
    "W R R R R W W W W W . W W W W\n"
    "W R R R R . G G G G . ^ . W W\n"
    "W R R R R W G G G G W W W W W\n"
    "W R R R R W G G G G W W W W W\n"
    "W W W W W W W W W W W W W W W\n"
    "=== FLOOR 1: SANCTUM ===\n"
    "W W W W W W W W W W W W W W W\n"
    "W P P P P P P P P P W B B B W\n"
    "W P P P P P P P P P W B B B W\n"
    "W P P P P P P P P P . B B B W\n"
    "W P P P P P P P P P W B B B W\n"
    "W P P P P P P P P P W B B B W\n"
    "W P P P P P P P P P . v . W W\n"
    "W P P P P P P P P P W W W W W\n"
    "W P P P P P P P P P W W W W W\n"
    "W W W W W W W W W W W W W W W\n";

static int test_two_floor_stacked(void) {
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    int rc = srd_generate_svo(TWO_FLOOR_GRID, 42, 1.0, &svo);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(svo.max_depth > 0);

    /* SVO bounds should span Y range covering both floors.
     * Default: room_height=4.0, floor_spacing=5.0.
     * Floor 0: cy=2,hy=2 → Y=[0,4]. Floor 1: cy=7,hy=2 → Y=[5,9].
     * With margin, max_y > 9. */
    ASSERT_TRUE(svo.world_bounds.max.y > 7.0f);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test: custom config with srd_generate_svo_ex ────────────── */

static int test_generate_svo_ex_custom_config(void) {
    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    srd_dungeon_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.cell_size     = 3.0f;   /* Larger cells */
    cfg.room_height   = 5.0f;   /* Taller rooms */
    cfg.floor_spacing = 7.0f;   /* Big gap between floors */
    cfg.voxel_size    = 0.5f;
    cfg.margin        = 2.0f;
    cfg.stair_steps   = 10;

    int rc = srd_generate_svo_ex(TWO_FLOOR_GRID, 42, 1.0, &cfg, &svo);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(svo.max_depth > 0);

    /* Floor 0: cy=2.5, hy=2.5 → Y=[0,5].
     * Floor 1: cy=9.5, hy=2.5 → Y=[7,12].
     * Gap between Y=5 and Y=7 is solid slab. */
    ASSERT_TRUE(svo.world_bounds.max.y > 10.0f);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test: single floor with === header still works ─────────── */

static int test_single_floor_with_header(void) {
    static const char *grid =
        "=== FLOOR 0: DUNGEON ===\n"
        "EEGG\n"
        "EEGG\n";

    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    int rc = srd_generate_svo(grid, 42, 0.5, &svo);
    ASSERT_INT_EQ(0, rc);
    ASSERT_TRUE(svo.max_depth > 0);

    /* Should succeed without crashing — single floor with header */

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"generate_svo_simple",          test_generate_svo_simple},
    {"generate_svo_three_rooms",     test_generate_svo_three_rooms},
    {"null_inputs",                  test_null_inputs},
    {"empty_grid",                   test_empty_grid},
    {"legacy_generate",              test_legacy_generate},
    {"two_floor_stacked",            test_two_floor_stacked},
    {"single_floor_with_header",     test_single_floor_with_header},
    {"generate_svo_ex_custom_config", test_generate_svo_ex_custom_config},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_bridge_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
