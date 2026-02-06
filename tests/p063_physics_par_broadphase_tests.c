/**
 * @file p063_physics_par_broadphase_tests.c
 * @brief Unit tests for parallel broadphase (phys-304).
 *
 * Validates that the parallel broadphase produces identical pair sets
 * to the sequential version, handles edge cases (zero bodies, no
 * overlap), and produces deterministic results across runs.
 */

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/par/broadphase_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/tier_list.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define TEST_FAIL(msg, ...)                                                    \
    do {                                                                        \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__,           \
                ##__VA_ARGS__);                                                \
        return 1;                                                              \
    } while (0)

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                         \
            TEST_FAIL("%s", #cond);                                            \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_UINT(expected, actual)                                       \
    do {                                                                        \
        unsigned long long _exp = (unsigned long long)(expected);               \
        unsigned long long _act = (unsigned long long)(actual);                 \
        if (_exp != _act) {                                                    \
            TEST_FAIL("expected %llu got %llu", _exp, _act);                   \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ── Constants ──────────────────────────────────────────────────── */

#define TEST_ARENA_SIZE (4u * 1024u * 1024u)
#define TEST_GRID_CELLS 64u
#define TEST_GRID_CELL_SIZE 4.0f
#define TEST_MAX_PAIRS 2048u

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Initialize a dynamic body at a given position with radius-1 sphere.
 */
static void make_dynamic_at(phys_body_t *body, float px, float py, float pz) {
    phys_body_init(body);
    phys_body_set_mass(body, 1.0f);
    body->position = (phys_vec3_t){px, py, pz};
    body->tier = PHYS_TIER_0_DIRECT;
}

/**
 * @brief Compare two collision pairs by (body_a, body_b) for qsort.
 */
static int pair_cmp(const void *a, const void *b) {
    const phys_collision_pair_t *pa = a;
    const phys_collision_pair_t *pb = b;
    if (pa->body_a != pb->body_a) {
        return (pa->body_a < pb->body_a) ? -1 : 1;
    }
    return (pa->body_b < pb->body_b) ? -1 : (pa->body_b > pb->body_b) ? 1 : 0;
}

/**
 * @brief Check if two sorted pair arrays are identical as sets.
 */
static bool pair_sets_equal(phys_collision_pair_t *a, uint32_t a_count,
                            phys_collision_pair_t *b, uint32_t b_count) {
    if (a_count != b_count) {
        return false;
    }
    qsort(a, a_count, sizeof(*a), pair_cmp);
    qsort(b, b_count, sizeof(*b), pair_cmp);
    return memcmp(a, b, a_count * sizeof(*a)) == 0;
}

/**
 * @brief Set up job system and physics job context.
 */
static void setup_job_ctx(job_system_t *sys, phys_job_context_t *ctx) {
    job_system_create(sys, 2, 256, 65536, 64, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}

/**
 * @brief Tear down job system and physics job context.
 */
static void teardown_job_ctx(job_system_t *sys, phys_job_context_t *ctx) {
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: 50 bodies in proximity. Compare par vs seq pair lists as sets.
 */
static int test_par_bp_identical_to_seq(void) {
    const uint32_t N = 50;

    phys_body_t bodies[50];
    phys_aabb_t aabbs[50];

    /* Place bodies in a 5x5x2 grid, spacing 1.5 (overlapping with r=1). */
    for (uint32_t i = 0; i < N; ++i) {
        float x = (float)(i % 5) * 1.5f;
        float y = (float)((i / 5) % 5) * 1.5f;
        float z = (float)(i / 25) * 1.5f;
        make_dynamic_at(&bodies[i], x, y, z);
        phys_aabb_from_sphere(&aabbs[i], bodies[i].position, 1.0f);
    }

    /* --- Sequential run --- */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid_seq;
    phys_spatial_grid_init(&grid_seq, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE,
                           &arena_seq);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid_seq, i, &aabbs[i]);
    }

    phys_tier_lists_t lists_seq;
    phys_tier_lists_init(&lists_seq, &arena_seq, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists_seq.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t pairs_seq[TEST_MAX_PAIRS];
    uint32_t seq_count = 0;

    phys_broadphase_args_t args_seq = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid_seq,
        .tier_lists = &lists_seq,
        .pairs_out = pairs_seq,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &seq_count,
    };
    phys_stage_broadphase(&args_seq);

    /* --- Parallel run --- */
    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid_par;
    phys_spatial_grid_init(&grid_par, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE,
                           &arena_par);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid_par, i, &aabbs[i]);
    }

    phys_tier_lists_t lists_par;
    phys_tier_lists_init(&lists_par, &arena_par, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists_par.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t pairs_par[TEST_MAX_PAIRS];
    uint32_t par_count = 0;

    phys_broadphase_args_t args_par = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid_par,
        .tier_lists = &lists_par,
        .pairs_out = pairs_par,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &par_count,
    };
    phys_stage_broadphase_par(&args_par, &ctx, &arena_par);

    /* Both must produce same pair set. */
    ASSERT_TRUE(seq_count > 0);
    ASSERT_TRUE(pair_sets_equal(pairs_seq, seq_count, pairs_par, par_count));

    teardown_job_ctx(&sys, &ctx);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    return 0;
}

/**
 * Test 2: Few bodies → should result in only 1 job batch.
 */
static int test_par_bp_single_batch(void) {
    const uint32_t N = 3;

    phys_body_t bodies[3];
    phys_aabb_t aabbs[3];

    make_dynamic_at(&bodies[0], 0, 0, 0);
    make_dynamic_at(&bodies[1], 1.5f, 0, 0);
    make_dynamic_at(&bodies[2], 0.5f, 0.5f, 0);

    for (uint32_t i = 0; i < N; ++i) {
        phys_aabb_from_sphere(&aabbs[i], bodies[i].position, 1.0f);
    }

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid, i, &aabbs[i]);
    }

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t pairs[TEST_MAX_PAIRS];
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase_par(&args, &ctx, &arena);

    /* 3 mutually overlapping bodies → 3 pairs. */
    ASSERT_EQ_UINT(3, pair_count);

    teardown_job_ctx(&sys, &ctx);
    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 3: Many bodies → multiple job batches.
 */
static int test_par_bp_multiple_batches(void) {
    const uint32_t N = 200;

    phys_body_t *bodies = calloc(N, sizeof(phys_body_t));
    phys_aabb_t *aabbs = calloc(N, sizeof(phys_aabb_t));
    ASSERT_TRUE(bodies && aabbs);

    /* Spread bodies in a 10x10x2 grid, spacing 1.5. */
    for (uint32_t i = 0; i < N; ++i) {
        float x = (float)(i % 10) * 1.5f;
        float y = (float)((i / 10) % 10) * 1.5f;
        float z = (float)(i / 100) * 1.5f;
        make_dynamic_at(&bodies[i], x, y, z);
        phys_aabb_from_sphere(&aabbs[i], bodies[i].position, 1.0f);
    }

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, TEST_GRID_CELL_SIZE, &arena);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid, i, &aabbs[i]);
    }

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t *pairs = calloc(TEST_MAX_PAIRS, sizeof(*pairs));
    ASSERT_TRUE(pairs);
    uint32_t pair_count = 0;

    phys_broadphase_args_t args = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase_par(&args, &ctx, &arena);

    /* With 200 closely spaced bodies, expect many pairs. */
    ASSERT_TRUE(pair_count > 0);

    /* Verify same as sequential. */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid_seq;
    phys_spatial_grid_init(&grid_seq, 256, TEST_GRID_CELL_SIZE, &arena_seq);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid_seq, i, &aabbs[i]);
    }

    phys_tier_lists_t lists_seq;
    phys_tier_lists_init(&lists_seq, &arena_seq, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists_seq.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t *pairs_seq = calloc(TEST_MAX_PAIRS, sizeof(*pairs_seq));
    ASSERT_TRUE(pairs_seq);
    uint32_t seq_count = 0;

    phys_broadphase_args_t args_seq = {
        .bodies = bodies,
        .aabbs = aabbs,
        .grid = &grid_seq,
        .tier_lists = &lists_seq,
        .pairs_out = pairs_seq,
        .max_pairs = TEST_MAX_PAIRS,
        .pair_count_out = &seq_count,
    };
    phys_stage_broadphase(&args_seq);

    ASSERT_TRUE(pair_sets_equal(pairs, pair_count, pairs_seq, seq_count));

    teardown_job_ctx(&sys, &ctx);
    phys_frame_arena_destroy(&arena);
    phys_frame_arena_destroy(&arena_seq);
    free(bodies);
    free(aabbs);
    free(pairs);
    free(pairs_seq);
    return 0;
}

/**
 * Test 4: Zero bodies → no crash, 0 pairs.
 */
static int test_par_bp_zero_bodies(void) {
    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE, &arena);

    phys_tier_lists_t lists;
    phys_tier_lists_init(&lists, &arena, 0);

    phys_collision_pair_t pairs[16];
    uint32_t pair_count = 99;

    phys_broadphase_args_t args = {
        .bodies = NULL,
        .aabbs = NULL,
        .grid = &grid,
        .tier_lists = &lists,
        .pairs_out = pairs,
        .max_pairs = 16,
        .pair_count_out = &pair_count,
    };
    phys_stage_broadphase_par(&args, &ctx, &arena);

    ASSERT_EQ_UINT(0, pair_count);

    teardown_job_ctx(&sys, &ctx);
    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 5: Bodies far apart → 0 pairs in both sequential and parallel.
 */
static int test_par_bp_no_overlap(void) {
    const uint32_t N = 10;

    phys_body_t bodies[10];
    phys_aabb_t aabbs[10];

    /* Space bodies 100 units apart so no overlap. */
    for (uint32_t i = 0; i < N; ++i) {
        make_dynamic_at(&bodies[i], (float)i * 100.0f, 0, 0);
        phys_aabb_from_sphere(&aabbs[i], bodies[i].position, 1.0f);
    }

    /* --- Sequential --- */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid_seq;
    phys_spatial_grid_init(&grid_seq, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE,
                           &arena_seq);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid_seq, i, &aabbs[i]);
    }

    phys_tier_lists_t lists_seq;
    phys_tier_lists_init(&lists_seq, &arena_seq, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists_seq.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t pairs_seq[64];
    uint32_t seq_count = 0;

    phys_broadphase_args_t args_seq = {
        .bodies = bodies, .aabbs = aabbs, .grid = &grid_seq,
        .tier_lists = &lists_seq, .pairs_out = pairs_seq,
        .max_pairs = 64, .pair_count_out = &seq_count,
    };
    phys_stage_broadphase(&args_seq);
    ASSERT_EQ_UINT(0, seq_count);

    /* --- Parallel --- */
    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);

    phys_spatial_grid_t grid_par;
    phys_spatial_grid_init(&grid_par, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE,
                           &arena_par);
    for (uint32_t i = 0; i < N; ++i) {
        phys_spatial_grid_insert(&grid_par, i, &aabbs[i]);
    }

    phys_tier_lists_t lists_par;
    phys_tier_lists_init(&lists_par, &arena_par, N);
    for (uint32_t i = 0; i < N; ++i) {
        phys_tier_list_add(&lists_par.tiers[PHYS_TIER_0_DIRECT], i);
    }

    phys_collision_pair_t pairs_par[64];
    uint32_t par_count = 0;

    phys_broadphase_args_t args_par = {
        .bodies = bodies, .aabbs = aabbs, .grid = &grid_par,
        .tier_lists = &lists_par, .pairs_out = pairs_par,
        .max_pairs = 64, .pair_count_out = &par_count,
    };
    phys_stage_broadphase_par(&args_par, &ctx, &arena_par);

    ASSERT_EQ_UINT(0, par_count);

    teardown_job_ctx(&sys, &ctx);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    return 0;
}

/**
 * Test 6: Run twice with same input → same pairs (as set).
 */
static int test_par_bp_deterministic(void) {
    const uint32_t N = 30;

    phys_body_t bodies[30];
    phys_aabb_t aabbs[30];

    for (uint32_t i = 0; i < N; ++i) {
        float x = (float)(i % 6) * 1.5f;
        float y = (float)(i / 6) * 1.5f;
        make_dynamic_at(&bodies[i], x, y, 0);
        phys_aabb_from_sphere(&aabbs[i], bodies[i].position, 1.0f);
    }

    phys_collision_pair_t pairs_a[TEST_MAX_PAIRS];
    phys_collision_pair_t pairs_b[TEST_MAX_PAIRS];
    uint32_t count_a = 0, count_b = 0;

    /* Run A */
    {
        job_system_t sys;
        phys_job_context_t ctx;
        setup_job_ctx(&sys, &ctx);

        phys_frame_arena_t arena;
        phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

        phys_spatial_grid_t grid;
        phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE,
                               &arena);
        for (uint32_t i = 0; i < N; ++i) {
            phys_spatial_grid_insert(&grid, i, &aabbs[i]);
        }

        phys_tier_lists_t lists;
        phys_tier_lists_init(&lists, &arena, N);
        for (uint32_t i = 0; i < N; ++i) {
            phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], i);
        }

        phys_broadphase_args_t args = {
            .bodies = bodies, .aabbs = aabbs, .grid = &grid,
            .tier_lists = &lists, .pairs_out = pairs_a,
            .max_pairs = TEST_MAX_PAIRS, .pair_count_out = &count_a,
        };
        phys_stage_broadphase_par(&args, &ctx, &arena);

        teardown_job_ctx(&sys, &ctx);
        phys_frame_arena_destroy(&arena);
    }

    /* Run B */
    {
        job_system_t sys;
        phys_job_context_t ctx;
        setup_job_ctx(&sys, &ctx);

        phys_frame_arena_t arena;
        phys_frame_arena_init(&arena, TEST_ARENA_SIZE);

        phys_spatial_grid_t grid;
        phys_spatial_grid_init(&grid, TEST_GRID_CELLS, TEST_GRID_CELL_SIZE,
                               &arena);
        for (uint32_t i = 0; i < N; ++i) {
            phys_spatial_grid_insert(&grid, i, &aabbs[i]);
        }

        phys_tier_lists_t lists;
        phys_tier_lists_init(&lists, &arena, N);
        for (uint32_t i = 0; i < N; ++i) {
            phys_tier_list_add(&lists.tiers[PHYS_TIER_0_DIRECT], i);
        }

        phys_broadphase_args_t args = {
            .bodies = bodies, .aabbs = aabbs, .grid = &grid,
            .tier_lists = &lists, .pairs_out = pairs_b,
            .max_pairs = TEST_MAX_PAIRS, .pair_count_out = &count_b,
        };
        phys_stage_broadphase_par(&args, &ctx, &arena);

        teardown_job_ctx(&sys, &ctx);
        phys_frame_arena_destroy(&arena);
    }

    ASSERT_TRUE(count_a > 0);
    ASSERT_TRUE(pair_sets_equal(pairs_a, count_a, pairs_b, count_b));

    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_bp_identical_to_seq",  test_par_bp_identical_to_seq},
    {"par_bp_single_batch",      test_par_bp_single_batch},
    {"par_bp_multiple_batches",  test_par_bp_multiple_batches},
    {"par_bp_zero_bodies",       test_par_bp_zero_bodies},
    {"par_bp_no_overlap",        test_par_bp_no_overlap},
    {"par_bp_deterministic",     test_par_bp_deterministic},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        fflush(stdout);
        int rc = tc->fn();
        if (rc == 0) {
            passed++;
            printf("OK %s\n", tc->name);
        } else {
            fprintf(stderr, "Test failed: %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
