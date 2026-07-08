/**
 * @file srd_critic_value_tests.c
 * @brief Critic value tests: verify each grid-based critic term
 *        responds correctly to specific grid modifications.
 *
 * Tests that critic terms increase/decrease as expected when rooms
 * are too small, unreachable, touching boundaries, or have bad
 * type adjacency.
 */
#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_sdf_grid.h"
#include "ferrum/procgen/srd/srd_room_map.h"
#include "ferrum/procgen/srd/srd_rules_wall.h"

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

/* ── Helper: build healthy layout (no critic penalties) ──────────── */

static int build_healthy_layout(srd_sdf_grid_t *grid, srd_room_map_t *map) {
    float origin[3] = {0.0f, 0.0f, 0.0f};
    if (srd_sdf_grid_init(grid, 32, 16, 32, 0.5f, origin) != 0) return -1;
    if (srd_room_map_init(map, 32, 16, 32) != 0) return -1;

    /* Two adjacent rooms, well away from grid boundaries */
    srd_sdf_grid_stamp_box(grid, 6.0f, 4.0f, 6.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r1 = srd_room_map_add_room(map, SRD_ROOM_ENTRANCE);
    srd_room_map_stamp_from_sdf(map, grid, r1);

    srd_sdf_grid_stamp_box(grid, 6.0f, 4.0f, 12.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r2 = srd_room_map_add_room(map, SRD_ROOM_GENERIC);
    srd_room_map_stamp_from_sdf(map, grid, r2);

    /* Connect them: carve doorway (z half-extent must overlap both rooms) */
    srd_sdf_grid_stamp_box(grid, 6.0f, 4.0f, 9.0f, 0.5f, 1.5f, 1.5f);

    srd_room_map_set_adjacent(map, r1, r2, true);

    return 0;
}

/* ── Test: healthy layout has zero loss ──────────────────────────── */

static int test_healthy_zero_loss(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_healthy_layout(&grid, &map));

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Healthy layout should have zero or near-zero loss */
    ASSERT_TRUE(r.total < 0.01f);
    ASSERT_TRUE(r.volume < 0.01f);
    ASSERT_TRUE(r.bounds < 0.01f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: volume term increases for tiny rooms ──────────────────── */

static int test_volume_penalty_small_room(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    ASSERT_INT_EQ(0, srd_sdf_grid_init(&grid, 16, 16, 16, 0.5f, origin));
    ASSERT_INT_EQ(0, srd_room_map_init(&map, 16, 16, 16));

    /* Create a very small room (just a few voxels) */
    srd_sdf_grid_stamp_box(&grid, 4.0f, 4.0f, 4.0f, 0.3f, 0.3f, 0.3f);
    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_ENTRANCE);
    srd_room_map_stamp_from_sdf(&map, &grid, r1);

    int vol = srd_room_map_count_volume(&map, r1);

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);
    /* Set min_room_voxels high so the tiny room triggers penalty */
    cfg.min_room_voxels = vol + 100;

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Volume penalty should be positive */
    ASSERT_TRUE(r.volume > 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: bounds penalty for room at grid edge ──────────────────── */

static int test_bounds_penalty_edge_room(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    ASSERT_INT_EQ(0, srd_sdf_grid_init(&grid, 16, 16, 16, 0.5f, origin));
    ASSERT_INT_EQ(0, srd_room_map_init(&map, 16, 16, 16));

    /* Room that touches the grid boundary (starts at x=0) */
    srd_sdf_grid_stamp_box(&grid, 1.0f, 4.0f, 4.0f, 1.5f, 2.0f, 2.0f);
    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_ENTRANCE);
    srd_room_map_stamp_from_sdf(&map, &grid, r1);

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Bounds penalty should be positive */
    ASSERT_TRUE(r.bounds > 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: separation penalty for bad type adjacency ─────────────── */

static int test_separation_penalty(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0.0f, 0.0f, 0.0f};
    ASSERT_INT_EQ(0, srd_sdf_grid_init(&grid, 32, 16, 32, 0.5f, origin));
    ASSERT_INT_EQ(0, srd_room_map_init(&map, 32, 16, 32));

    /* Boss room adjacent to entrance — bad pairing */
    srd_sdf_grid_stamp_box(&grid, 4.0f, 4.0f, 4.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r1 = srd_room_map_add_room(&map, SRD_ROOM_ENTRANCE);
    srd_room_map_stamp_from_sdf(&map, &grid, r1);

    srd_sdf_grid_stamp_box(&grid, 4.0f, 4.0f, 10.0f, 2.5f, 2.0f, 2.5f);
    uint8_t r2 = srd_room_map_add_room(&map, SRD_ROOM_BOSS);
    srd_room_map_stamp_from_sdf(&map, &grid, r2);

    srd_room_map_set_adjacent(&map, r1, r2, true);

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Separation penalty should be positive for boss adjacent to entrance */
    ASSERT_TRUE(r.separation > 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: wall push increases loss (room gets smaller) ──────────── */

static int test_critic_responds_to_wall_push(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    ASSERT_INT_EQ(0, build_healthy_layout(&grid, &map));

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);
    /* Set min_room_voxels to current volume so pushing increases penalty */
    int vol = srd_room_map_count_volume(&map, 1);
    cfg.min_room_voxels = vol;

    srd_grid_critic_result_t before = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Push wall inward (shrink room) */
    srd_voxel_selection_t sel = {
        .room_id = 1, .face = SRD_FACE_EAST, .corner = -1, .param = 2.0f
    };
    ASSERT_INT_EQ(0, srd_rule_wall_push(&grid, &map, &sel));

    srd_grid_critic_result_t after = srd_grid_critic_evaluate(&grid, &map, &cfg);

    /* Loss should increase (room is now below min_room_voxels) */
    ASSERT_TRUE(after.volume > before.volume);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: null inputs return zero loss ──────────────────────────── */

static int test_critic_null_inputs(void) {
    srd_sdf_grid_t grid;
    srd_room_map_t map;
    float origin[3] = {0};
    srd_sdf_grid_init(&grid, 4, 4, 4, 0.5f, origin);
    srd_room_map_init(&map, 4, 4, 4);

    srd_grid_critic_config_t cfg;
    srd_grid_critic_config_default(&cfg);

    srd_grid_critic_result_t r1 = srd_grid_critic_evaluate(NULL, &map, &cfg);
    ASSERT_TRUE(r1.total == 0.0f);

    srd_grid_critic_result_t r2 = srd_grid_critic_evaluate(&grid, NULL, &cfg);
    ASSERT_TRUE(r2.total == 0.0f);

    srd_grid_critic_result_t r3 = srd_grid_critic_evaluate(&grid, &map, NULL);
    ASSERT_TRUE(r3.total == 0.0f);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"healthy_zero_loss",              test_healthy_zero_loss},
    {"volume_penalty_small_room",      test_volume_penalty_small_room},
    {"bounds_penalty_edge_room",       test_bounds_penalty_edge_room},
    {"separation_penalty",             test_separation_penalty},
    {"critic_responds_to_wall_push",   test_critic_responds_to_wall_push},
    {"critic_null_inputs",             test_critic_null_inputs},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_critic_value_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
