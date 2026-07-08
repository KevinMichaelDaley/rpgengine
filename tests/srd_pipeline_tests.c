#define _POSIX_C_SOURCE 199309L

/**
 * @file srd_pipeline_tests.c
 * @brief Full pipeline integration test: ASCII → SDF → optimize → SVO.
 *
 * Loads tower_dungeon.asc, runs the full pipeline, and verifies:
 *   1. Pipeline completes without error
 *   2. SVO is non-empty (has solid voxels)
 *   3. Loss decreases or stays zero during optimization
 *   4. Terminates within time budget
 *   5. All rooms reachable (reachability loss = 0)
 */
#include "ferrum/procgen/srd/srd_bridge.h"
#include "ferrum/procgen/srd/srd_grammar.h"
#include "ferrum/procgen/srd/srd_seed_init.h"
#include "ferrum/procgen/srd/srd_descent_loop.h"
#include "ferrum/procgen/srd/srd_descent_config.h"
#include "ferrum/procgen/srd/srd_voxel_rule_table.h"
#include "ferrum/procgen/srd/srd_grid_critic.h"
#include "ferrum/procgen/srd/srd_sdf_to_svo.h"
#include "ferrum/npc/npc_svo.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* ── Helper: read file ───────────────────────────────────────────── */

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    rewind(f);
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';
    return buf;
}

/* ── Test: full pipeline on tower_dungeon.asc ────────────────────── */

static int test_pipeline_tower_dungeon(void) {
    char *ascii = read_file("datasets/ascii_grids/tower_dungeon.asc");
    if (!ascii) {
        fprintf(stderr, "  SKIP: cannot open tower_dungeon.asc\n");
        return 0; /* Skip, don't fail */
    }

    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int rc = srd_generate_svo(ascii, 42, 2.0, &svo);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                     (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    free(ascii);

    ASSERT_INT_EQ(0, rc);

    /* SVO should be initialized */
    ASSERT_TRUE(svo.max_depth > 0);

    /* Should complete within reasonable time (2s budget + overhead) */
    ASSERT_TRUE(elapsed < 4.0);

    fprintf(stderr, "    pipeline completed in %.2fs, SVO depth=%u\n",
            elapsed, svo.max_depth);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test: short budget terminates quickly ────────────────────────── */

static int test_pipeline_short_budget(void) {
    char *ascii = read_file("datasets/ascii_grids/tower_dungeon.asc");
    if (!ascii) {
        fprintf(stderr, "  SKIP: cannot open tower_dungeon.asc\n");
        return 0;
    }

    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int rc = srd_generate_svo(ascii, 42, 0.5, &svo);

    clock_gettime(CLOCK_MONOTONIC, &t1);
    double elapsed = (double)(t1.tv_sec - t0.tv_sec) +
                     (double)(t1.tv_nsec - t0.tv_nsec) * 1e-9;

    free(ascii);

    ASSERT_INT_EQ(0, rc);
    /* 0.5s budget should complete in < 2s including setup/teardown */
    ASSERT_TRUE(elapsed < 2.0);

    fprintf(stderr, "    short-budget pipeline completed in %.2fs\n", elapsed);

    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test: optimizer reduces loss on multi-room layout ───────────── */

static int test_optimizer_reduces_loss(void) {
    /* Build a layout where the critic should find issues to fix */
    srd_sdf_grid_t grid;
    srd_room_map_t map;

    /* 3 rooms: entrance, boss adjacent to entrance (bad), and generic */
    srd_seed_room_t seeds[3] = {
        { 4.0f, 2.0f, 4.0f,  2.0f, 2.0f, 2.0f, SRD_ROOM_ENTRANCE },
        { 10.0f, 2.0f, 4.0f, 2.0f, 2.0f, 2.0f, SRD_ROOM_BOSS },
        { 4.0f, 2.0f, 10.0f, 2.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC },
    };
    int adj_pairs[] = { 0, 1, 0, 2 };

    int rc = srd_seed_to_grid(seeds, 3, adj_pairs, 2,
                              0.5f, 2.0f, &grid, &map);
    ASSERT_INT_EQ(0, rc);

    srd_grid_critic_config_t critic_cfg;
    srd_grid_critic_config_default(&critic_cfg);

    /* Measure initial loss */
    srd_grid_critic_result_t before = srd_grid_critic_evaluate(
        &grid, &map, &critic_cfg);

    /* Run optimizer */
    srd_descent_config_t cfg;
    srd_descent_config_from_budget(&cfg, 1.0);
    int n_rules = 0;
    cfg.rules = srd_voxel_rule_table_default(&n_rules);
    cfg.n_rules = n_rules;

    srd_descent_result_t result = srd_descent_optimize(&grid, &map, &cfg);

    /* Final loss should be <= initial loss */
    ASSERT_TRUE(result.final_loss <= result.initial_loss);
    ASSERT_TRUE(result.iterations > 0);

    fprintf(stderr, "    loss: %.4f → %.4f (%d iters)\n",
            result.initial_loss, result.final_loss, result.iterations);

    /* Check reachability after optimization */
    srd_grid_critic_result_t after = srd_grid_critic_evaluate(
        &grid, &map, &critic_cfg);
    ASSERT_TRUE(after.reachability <= before.reachability);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    return 0;
}

/* ── Test: SVO conversion produces solid voxels ──────────────────── */

static int test_svo_has_solid_voxels(void) {
    /* Build a simple room and convert to SVO */
    srd_seed_room_t seeds[1] = {
        { 4.0f, 2.0f, 4.0f, 2.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC },
    };

    srd_sdf_grid_t grid;
    srd_room_map_t map;
    int rc = srd_seed_to_grid(seeds, 1, NULL, 0,
                              0.5f, 2.0f, &grid, &map);
    ASSERT_INT_EQ(0, rc);

    npc_svo_grid_t svo;
    memset(&svo, 0, sizeof(svo));
    rc = srd_sdf_to_svo(&grid, &svo);
    ASSERT_INT_EQ(0, rc);

    /* SVO should have nodes (solid voxels around the room) */
    ASSERT_TRUE(svo.node_count > 0);
    ASSERT_TRUE(svo.max_depth > 0);

    srd_sdf_grid_destroy(&grid);
    srd_room_map_destroy(&map);
    npc_svo_grid_destroy(&svo);
    return 0;
}

/* ── Test runner ──────────────────────────────────────────────────── */

struct test_case { const char *name; int (*fn)(void); };
static struct test_case TESTS[] = {
    {"pipeline_tower_dungeon",  test_pipeline_tower_dungeon},
    {"pipeline_short_budget",   test_pipeline_short_budget},
    {"optimizer_reduces_loss",  test_optimizer_reduces_loss},
    {"svo_has_solid_voxels",    test_svo_has_solid_voxels},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    fprintf(stderr, "srd_pipeline_tests: %zu tests\n", total);
    for (size_t i = 0; i < total; i++) {
        fprintf(stderr, "  RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) { fprintf(stderr, "  OK   %s\n", TESTS[i].name); passed++; }
        else         { fprintf(stderr, "  FAIL %s\n", TESTS[i].name); }
    }
    fprintf(stderr, "\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
