#define _POSIX_C_SOURCE 199309L

/**
 * @file srd_descent_loop_tests.c
 * @brief Tests for the voxel-grid descent loop (pure discrete).
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"

#include <stdio.h>
#include <time.h>

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

/* ── Helper: build a 4-room layout ──────────────────────────────── */

/**
 * Build a 32x16x32 grid with 4 rooms in a 2x2 arrangement.
 * Room 1: (4,4,4)  hw=2.5, hh=2.0, hd=2.5  (entrance)
 * Room 2: (12,4,4) hw=2.5, hh=2.0, hd=2.5  (generic)
 * Room 3: (4,4,12) hw=2.5, hh=2.0, hd=2.5  (generic)
 * Room 4: (12,4,12) hw=2.5, hh=2.0, hd=2.5 (boss)
 *
 * Rooms are separated so the optimizer has something to work with.
 */
static int build_four_room_layout(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 32, 16, 32, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 32, 16, 32) != 0) return -1;

    /* Room 1: entrance at (4,4,4) */
    srd_sdf_grid_stamp_box(grid, 4.0f, 4.0f, 4.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r1 = srd_room_map_add_room(map, SRD_ROOM_ENTRANCE);
    srd_room_map_stamp_from_sdf(map, grid, r1);

    /* Room 2: generic at (12,4,4) */
    srd_sdf_grid_stamp_box(grid, 12.0f, 4.0f, 4.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r2 = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, r2);

    /* Room 3: generic at (4,4,12) */
    srd_sdf_grid_stamp_box(grid, 4.0f, 4.0f, 12.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r3 = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, r3);

    /* Room 4: boss at (12,4,12) */
    srd_sdf_grid_stamp_box(grid, 12.0f, 4.0f, 12.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r4 = srd_room_map_add_room(map, SRD_ROOM_BOSS);
    srd_room_map_stamp_from_sdf(map, grid, r4);

    /* Set adjacency: 1-2, 2-4, 1-3, 3-4 (a grid) */
    srd_room_map_set_adjacent(map, r1, r2, true);
    srd_room_map_set_adjacent(map, r2, r4, true);
    srd_room_map_set_adjacent(map, r1, r3, true);
    srd_room_map_set_adjacent(map, r3, r4, true);

    return 0;
}

/* ── Test: loop terminates within budget ─────────────────────────── */

static int test_loop_terminates_in_budget(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_four_room_layout(&grid, &map));

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 0.5);

    /* Get the default rule table */
    int n_rules = 0;
    const srd_voxel_rule_entry_t *rules = srd_voxel_rule_table_default(&n_rules);
    cfg.rules = rules;
    cfg.n_rules = n_rules;

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    srd_descent_result_t result = srd_descent_optimize(&grid, &map, &cfg);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                     (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    /* Must finish within 110% of budget */
    ASSERT_TRUE(elapsed < cfg.time_budget_s * 1.1);
    ASSERT_TRUE(result.iterations >= 0);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: loss does not increase ────────────────────────────────── */

static int test_loss_non_increasing(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_four_room_layout(&grid, &map));

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);

    int n_rules = 0;
    const srd_voxel_rule_entry_t *rules = srd_voxel_rule_table_default(&n_rules);
    cfg.rules = rules;
    cfg.n_rules = n_rules;

    srd_descent_result_t result = srd_descent_optimize(&grid, &map, &cfg);

    /* Final loss should be <= initial loss (optimizer should not make it worse) */
    ASSERT_TRUE(result.final_loss <= result.initial_loss);
    ASSERT_TRUE(result.initial_loss >= 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: temperature decreases monotonically ──────────────────── */

static int test_temperature_decreases(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_four_room_layout(&grid, &map));

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 0.5);

    int n_rules = 0;
    const srd_voxel_rule_entry_t *rules = srd_voxel_rule_table_default(&n_rules);
    cfg.rules = rules;
    cfg.n_rules = n_rules;

    srd_descent_result_t result = srd_descent_optimize(&grid, &map, &cfg);

    /* Temperature should have decreased (at least one iteration) */
    if (result.iterations > 0) {
        ASSERT_TRUE(result.final_temperature < cfg.temperature_init);
        ASSERT_TRUE(result.final_temperature >= cfg.temperature_min);
    }

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: iterations positive with non-trivial budget ──────────── */

static int test_iterations_positive(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_four_room_layout(&grid, &map));

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);

    int n_rules = 0;
    const srd_voxel_rule_entry_t *rules = srd_voxel_rule_table_default(&n_rules);
    cfg.rules = rules;
    cfg.n_rules = n_rules;

    srd_descent_result_t result = srd_descent_optimize(&grid, &map, &cfg);

    /* With 1s budget, should have run multiple iterations */
    ASSERT_TRUE(result.iterations > 0);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: null inputs ──────────────────────────────────────────── */

static int test_null_inputs(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.5f, origin);
    srd_room_map_init(&map, 4, 4, 4);

    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 0.1);
    int n_rules = 0;
    const srd_voxel_rule_entry_t *rules = srd_voxel_rule_table_default(&n_rules);
    cfg.rules = rules;
    cfg.n_rules = n_rules;

    /* Null grid */
    srd_descent_result_t r1 = srd_descent_optimize(NULL, &map, &cfg);
    ASSERT_TRUE(r1.final_loss < 0.0f);

    /* Null map */
    srd_descent_result_t r2 = srd_descent_optimize(&grid, NULL, &cfg);
    ASSERT_TRUE(r2.final_loss < 0.0f);

    /* Null config */
    srd_descent_result_t r3 = srd_descent_optimize(&grid, &map, NULL);
    ASSERT_TRUE(r3.final_loss < 0.0f);

    /* Null rules */
    srd_descent_config_t bad_cfg = cfg;
    bad_cfg.rules = NULL;
    srd_descent_result_t r4 = srd_descent_optimize(&grid, &map, &bad_cfg);
    ASSERT_TRUE(r4.final_loss < 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: config from budget tiers ─────────────────────────────── */

static int test_config_tiers(void) {
    srd_descent_config_t cfg;

    /* Small budget */
    srd_descent_config_from_budget(&cfg, 1.0);
    ASSERT_TRUE(cfg.k_candidates > 0);
    ASSERT_TRUE(cfg.temperature_init > 0.0f);
    ASSERT_TRUE(cfg.temperature_decay > 0.0f && cfg.temperature_decay < 1.0f);
    ASSERT_TRUE(cfg.temperature_min > 0.0f);

    int k_small = cfg.k_candidates;

    /* Larger budget should have more candidates */
    srd_descent_config_from_budget(&cfg, 30.0);
    ASSERT_TRUE(cfg.k_candidates >= k_small);

    return 0;
}

/* ── Test: room_map_copy ─────────────────────────────────────────── */

static int test_room_map_copy(void) {
    srd_room_map_t src, dst;
    ASSERT_INT_EQ(0, srd_room_map_init(&src, 8, 8, 8));

    uint8_t r1 = srd_room_map_add_room(&src, SRD_ROOM_GENERIC);
    srd_room_map_set(&src, 2, 3, 4, r1);

    ASSERT_INT_EQ(0, srd_room_map_copy(&dst, &src));

    /* Verify copy has same data */
    ASSERT_INT_EQ(src.nx, dst.nx);
    ASSERT_INT_EQ(src.ny, dst.ny);
    ASSERT_INT_EQ(src.nz, dst.nz);
    ASSERT_INT_EQ(src.n_rooms, dst.n_rooms);
    ASSERT_INT_EQ(r1, srd_room_map_get(&dst, 2, 3, 4));

    /* Modifying copy should not affect original */
    srd_room_map_set(&dst, 2, 3, 4, 0);
    ASSERT_INT_EQ(r1, srd_room_map_get(&src, 2, 3, 4));

    srd_room_map_destroy(&src);
    srd_room_map_destroy(&dst);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"loop_terminates_in_budget", test_loop_terminates_in_budget},
    {"loss_non_increasing",       test_loss_non_increasing},
    {"temperature_decreases",     test_temperature_decreases},
    {"iterations_positive",       test_iterations_positive},
    {"null_inputs",               test_null_inputs},
    {"config_tiers",              test_config_tiers},
    {"room_map_copy",             test_room_map_copy},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_descent_loop_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
