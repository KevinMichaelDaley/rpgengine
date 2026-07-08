/**
 * @file srd_grid_critic_tests.c
 * @brief Tests for srd_grid_critic: volume, reachability, bounds, separation.
 *
 * TDD Phase 1 (RED): tests define the API before implementation.
 */
#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_seed_init.h"

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

#define ASSERT_FLOAT_GT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a > _b)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f > %.6f\n", \
                __FILE__, __LINE__, (double)_a, (double)_b); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps) do { \
    float _e = (exp), _a = (act); \
    if (fabsf(_e - _a) > (eps)) { \
        fprintf(stderr, "  FAIL %s:%d: expected %.6f, got %.6f (eps=%.6f)\n", \
                __FILE__, __LINE__, (double)_e, (double)_a, (double)(eps)); \
        return 1; \
    } \
} while (0)

/* ── Helper: build a simple 2-room test layout ────────────────── */

/**
 * Build a 2-room layout connected by a doorway.
 * Room 1 (ENTRANCE) at (4, 2, 4), Room 2 (GENERIC) at (8, 2, 4).
 * Grid at 0.25m voxels.
 */
static int build_two_room_layout(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    srd_seed_room_t rooms[2] = {
        { .cx = 4.0f, .cy = 2.0f, .cz = 4.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_ENTRANCE },
        { .cx = 8.0f, .cy = 2.0f, .cz = 4.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_GENERIC }
    };
    int adj[2] = {0, 1};
    return srd_seed_to_grid(rooms, 2, adj, 1, 0.25f, 1.0f, grid, map);
}

/* ── Test: default config ──────────────────────────────────────── */

static int test_default_config(void) {
    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    ASSERT_FLOAT_GT(cfg.w_volume, 0.0f);
    ASSERT_FLOAT_GT(cfg.w_reachability, 0.0f);
    ASSERT_FLOAT_GT(cfg.w_bounds, 0.0f);
    ASSERT_FLOAT_GT(cfg.w_separation, 0.0f);
    ASSERT_TRUE(cfg.min_room_voxels > 0);
    return 0;
}

/* ── Test: healthy layout has low loss ─────────────────────────── */

static int test_healthy_layout(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_two_room_layout(&grid, &map));

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Both rooms should have decent volume → volume term ≈ 0 */
    ASSERT_FLOAT_NEAR(0.0f, r.volume, 0.01f);

    /* Both rooms reachable from entrance → reachability term = 0 */
    ASSERT_FLOAT_NEAR(0.0f, r.reachability, 0.01f);

    /* Rooms don't touch grid edge (1m margin) → bounds term = 0 */
    ASSERT_FLOAT_NEAR(0.0f, r.bounds, 0.01f);

    /* Entrance adjacent to Generic is fine → separation term = 0 */
    ASSERT_FLOAT_NEAR(0.0f, r.separation, 0.01f);

    /* Total should be ≈ 0 */
    ASSERT_FLOAT_NEAR(0.0f, r.total, 0.01f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: volume term increases when room shrinks ─────────────── */

static int test_volume_penalty(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_two_room_layout(&grid, &map));

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);
    /* Set a high min volume so the rooms are "too small" */
    cfg.min_room_voxels = 100000;

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Volume term should be positive (rooms are smaller than threshold) */
    ASSERT_FLOAT_GT(r.volume, 0.0f);
    ASSERT_FLOAT_GT(r.total, 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: reachability fires when doorway is blocked ──────────── */

static int test_reachability_blocked(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_two_room_layout(&grid, &map));

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    /* Baseline: both rooms reachable */
    srd_grid_critic_result_t r1 = srd_grid_critic_evaluate(&grid, &map, &cfg);
    ASSERT_FLOAT_NEAR(0.0f, r1.reachability, 0.01f);

    /* Block the doorway: fill in a wall between rooms at x=6.
     * Room 1 spans X=[2,6], Room 2 spans X=[6,10].
     * Fill a box at the interface to seal them off. */
    srd_sdf_grid_subtract_box(&grid, 6.0f, 2.0f, 4.0f, 0.5f, 1.5f, 2.0f);

    srd_grid_critic_result_t r2 = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Reachability should now be positive (room 2 unreachable) */
    ASSERT_FLOAT_GT(r2.reachability, 0.0f);
    ASSERT_FLOAT_GT(r2.total, r1.total);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: bounds violation when room touches edge ─────────────── */

static int test_bounds_violation(void) {
    /* Create a room that touches the grid edge (no margin). */
    srd_seed_room_t rooms[1] = {{
        .cx = 2.0f, .cy = 2.0f, .cz = 2.0f,
        .hx = 2.0f, .hy = 2.0f, .hz = 2.0f,
        .type = SRD_ROOM_ENTRANCE
    }};

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    /* Very small margin — room will touch the edge */
    int rc = srd_seed_to_grid(rooms, 1, NULL, 0, 0.25f, 0.1f, &grid, &map);
    ASSERT_INT_EQ(0, rc);

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Room touches the grid boundary → bounds term > 0 */
    ASSERT_FLOAT_GT(r.bounds, 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: type separation penalty ─────────────────────────────── */

static int test_type_separation(void) {
    /* Boss room directly adjacent to entrance → bad */
    srd_seed_room_t rooms[2] = {
        { .cx = 4.0f, .cy = 2.0f, .cz = 4.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_ENTRANCE },
        { .cx = 8.0f, .cy = 2.0f, .cz = 4.0f,
          .hx = 2.0f, .hy = 1.5f, .hz = 2.0f,
          .type = SRD_ROOM_BOSS }
    };
    int adj[2] = {0, 1};

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, srd_seed_to_grid(rooms, 2, adj, 1, 0.25f, 1.0f,
                                       &grid, &map));

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Boss adjacent to entrance → separation penalty > 0 */
    ASSERT_FLOAT_GT(r.separation, 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: null inputs ─────────────────────────────────────────── */

static int test_null_inputs(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    /* Should return a result with total = 0 (or some safe default) */
    srd_grid_critic_result_t r;

    r = srd_grid_critic_evaluate(NULL, &map, &cfg);
    ASSERT_FLOAT_NEAR(0.0f, r.total, 0.01f);

    float origin[3] = {0};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.5f, origin);
    srd_room_map_init(&map, 4, 4, 4);

    r = srd_grid_critic_evaluate(&grid, NULL, &cfg);
    ASSERT_FLOAT_NEAR(0.0f, r.total, 0.01f);

    r = srd_grid_critic_evaluate(&grid, &map, NULL);
    ASSERT_FLOAT_NEAR(0.0f, r.total, 0.01f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ───────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"default_config",        test_default_config},
    {"healthy_layout",        test_healthy_layout},
    {"volume_penalty",        test_volume_penalty},
    {"reachability_blocked",  test_reachability_blocked},
    {"bounds_violation",      test_bounds_violation},
    {"type_separation",       test_type_separation},
    {"null_inputs",           test_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;

    fprintf(stderr, "srd_grid_critic_tests: %zu tests\n", total);
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
