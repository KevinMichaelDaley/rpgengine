/**
 * @file p061_physics_par_tier_tests.c
 * @brief Tests for parallel tier classification (phys-302).
 *
 * Validates that phys_stage_tier_classify_par produces identical results
 * to the sequential phys_stage_tier_classify across various body counts,
 * body types, and edge cases.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/par/tier_classify_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tier_classify.h"
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

/* ── Helpers ────────────────────────────────────────────────────── */

/** Arena size: 16 MiB — enough for large body counts with per-batch lists. */
#define TEST_ARENA_SIZE (16u * 1024u * 1024u)

/**
 * @brief Create a dynamic body with the given mass.
 */
static void make_dynamic(phys_body_t *body, float mass) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
}

/**
 * @brief Create a static body (inv_mass == 0).
 */
static void make_static(phys_body_t *body) {
    phys_body_init(body);
}

/**
 * @brief Create a sleeping dynamic body.
 */
static void make_sleeping(phys_body_t *body, float mass) {
    phys_body_init(body);
    phys_body_set_mass(body, mass);
    phys_body_set_sleeping(body, true);
}

/**
 * @brief Compare two tier lists for identical content (order-independent).
 *
 * Since parallel classification may produce indices in a different order
 * within each tier, we sort before comparing.
 */
static int uint32_cmp(const void *a, const void *b) {
    uint32_t va = *(const uint32_t *)a;
    uint32_t vb = *(const uint32_t *)b;
    if (va < vb) return -1;
    if (va > vb) return  1;
    return 0;
}

/**
 * @brief Verify that two tier_lists_t have identical contents
 *        (order-independent within each tier).
 * @return 0 if identical, 1 if different.
 */
static int compare_tier_lists(const phys_tier_lists_t *a,
                              const phys_tier_lists_t *b) {
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        if (a->tiers[t].count != b->tiers[t].count) {
            fprintf(stderr,
                    "  tier %d: count mismatch (seq=%u par=%u)\n",
                    t, a->tiers[t].count, b->tiers[t].count);
            return 1;
        }
        uint32_t n = a->tiers[t].count;
        if (n == 0) continue;

        /* Copy and sort both index arrays for order-independent compare. */
        uint32_t *sa = malloc(n * sizeof(uint32_t));
        uint32_t *sb = malloc(n * sizeof(uint32_t));
        if (!sa || !sb) {
            free(sa);
            free(sb);
            return 1;
        }
        memcpy(sa, a->tiers[t].indices, n * sizeof(uint32_t));
        memcpy(sb, b->tiers[t].indices, n * sizeof(uint32_t));
        qsort(sa, n, sizeof(uint32_t), uint32_cmp);
        qsort(sb, n, sizeof(uint32_t), uint32_cmp);

        int diff = memcmp(sa, sb, n * sizeof(uint32_t));
        free(sa);
        free(sb);
        if (diff != 0) {
            fprintf(stderr, "  tier %d: index mismatch\n", t);
            return 1;
        }
    }
    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: 100 bodies with various types. Sequential and parallel
 * results must be identical.
 */
static int test_par_tier_identical_to_seq(void) {
    const uint32_t N = 100;

    phys_body_t *bodies = calloc(N, sizeof(phys_body_t));
    uint8_t *active = calloc(N, sizeof(uint8_t));
    ASSERT_TRUE(bodies && active);

    /* Mix of dynamic, static, sleeping bodies. */
    for (uint32_t i = 0; i < N; ++i) {
        active[i] = 1;
        if (i % 7 == 0) {
            make_static(&bodies[i]);
        } else if (i % 5 == 0) {
            make_sleeping(&bodies[i], 1.0f);
        } else {
            make_dynamic(&bodies[i], 1.0f + (float)i * 0.1f);
        }
    }

    /* Sequential run. */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_seq;
    memset(&lists_seq, 0, sizeof(lists_seq));

    phys_tier_classify_args_t args_seq = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_seq, .arena = &arena_seq,
    };
    phys_stage_tier_classify(&args_seq);

    /* Parallel run. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_par;
    memset(&lists_par, 0, sizeof(lists_par));

    phys_tier_classify_args_t args_par = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_par, .arena = &arena_par,
    };
    phys_stage_tier_classify_par(&args_par, &ctx);

    /* Compare results. */
    int cmp = compare_tier_lists(&lists_seq, &lists_par);
    ASSERT_TRUE(cmp == 0);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    free(bodies);
    free(active);
    return 0;
}

/**
 * Test 2: 500 bodies (< 1024) → 1 job. Results match sequential.
 */
static int test_par_tier_single_batch(void) {
    const uint32_t N = 500;

    phys_body_t *bodies = calloc(N, sizeof(phys_body_t));
    uint8_t *active = calloc(N, sizeof(uint8_t));
    ASSERT_TRUE(bodies && active);

    for (uint32_t i = 0; i < N; ++i) {
        active[i] = 1;
        if (i % 3 == 0) {
            make_sleeping(&bodies[i], 2.0f);
        } else {
            make_dynamic(&bodies[i], 1.0f);
        }
    }

    /* Sequential. */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_seq;
    memset(&lists_seq, 0, sizeof(lists_seq));

    phys_tier_classify_args_t args_seq = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_seq, .arena = &arena_seq,
    };
    phys_stage_tier_classify(&args_seq);

    /* Parallel. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_par;
    memset(&lists_par, 0, sizeof(lists_par));

    phys_tier_classify_args_t args_par = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_par, .arena = &arena_par,
    };
    phys_stage_tier_classify_par(&args_par, &ctx);

    int cmp = compare_tier_lists(&lists_seq, &lists_par);
    ASSERT_TRUE(cmp == 0);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    free(bodies);
    free(active);
    return 0;
}

/**
 * Test 3: 3000 bodies → ceil(3000/1024) = 3 jobs. Results match sequential.
 */
static int test_par_tier_multiple_batches(void) {
    const uint32_t N = 3000;

    phys_body_t *bodies = calloc(N, sizeof(phys_body_t));
    uint8_t *active = calloc(N, sizeof(uint8_t));
    ASSERT_TRUE(bodies && active);

    for (uint32_t i = 0; i < N; ++i) {
        active[i] = (i % 10 != 0) ? 1 : 0; /* 10% inactive. */
        if (i % 11 == 0) {
            make_static(&bodies[i]);
        } else if (i % 4 == 0) {
            make_sleeping(&bodies[i], 1.5f);
        } else {
            make_dynamic(&bodies[i], 0.5f + (float)(i % 20));
        }
    }

    /* Sequential. */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_seq;
    memset(&lists_seq, 0, sizeof(lists_seq));

    phys_tier_classify_args_t args_seq = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_seq, .arena = &arena_seq,
    };
    phys_stage_tier_classify(&args_seq);

    /* Parallel. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_par;
    memset(&lists_par, 0, sizeof(lists_par));

    phys_tier_classify_args_t args_par = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_par, .arena = &arena_par,
    };
    phys_stage_tier_classify_par(&args_par, &ctx);

    int cmp = compare_tier_lists(&lists_seq, &lists_par);
    ASSERT_TRUE(cmp == 0);

    /* Verify batch count is 3. */
    uint32_t expected_batches = (N + 1023) / 1024;
    ASSERT_EQ_UINT(3, expected_batches);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    free(bodies);
    free(active);
    return 0;
}

/**
 * Test 4: 0 bodies → no crash, empty tier lists.
 */
static int test_par_tier_zero_bodies(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, TEST_ARENA_SIZE);
    phys_tier_lists_t lists;
    memset(&lists, 0, sizeof(lists));

    phys_tier_classify_args_t args = {
        .bodies = NULL, .active = NULL, .body_count = 0,
        .game = NULL, .tier_lists_out = &lists, .arena = &arena,
    };
    phys_stage_tier_classify_par(&args, &ctx);

    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        ASSERT_EQ_UINT(0, lists.tiers[t].count);
    }

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * Test 5: All bodies static (inv_mass=0). All should be excluded
 * from every tier list.
 */
static int test_par_tier_all_static(void) {
    const uint32_t N = 200;

    phys_body_t *bodies = calloc(N, sizeof(phys_body_t));
    uint8_t *active = calloc(N, sizeof(uint8_t));
    ASSERT_TRUE(bodies && active);

    for (uint32_t i = 0; i < N; ++i) {
        active[i] = 1;
        make_static(&bodies[i]);
    }

    /* Sequential. */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_seq;
    memset(&lists_seq, 0, sizeof(lists_seq));

    phys_tier_classify_args_t args_seq = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_seq, .arena = &arena_seq,
    };
    phys_stage_tier_classify(&args_seq);

    /* Parallel. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_par;
    memset(&lists_par, 0, sizeof(lists_par));

    phys_tier_classify_args_t args_par = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_par, .arena = &arena_par,
    };
    phys_stage_tier_classify_par(&args_par, &ctx);

    /* All tiers should be empty. */
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        ASSERT_EQ_UINT(0, lists_seq.tiers[t].count);
        ASSERT_EQ_UINT(0, lists_par.tiers[t].count);
    }

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    free(bodies);
    free(active);
    return 0;
}

/**
 * Test 6: 5000 bodies — verify correct results at scale.
 */
static int test_par_tier_scales_linearly(void) {
    const uint32_t N = 5000;

    phys_body_t *bodies = calloc(N, sizeof(phys_body_t));
    uint8_t *active = calloc(N, sizeof(uint8_t));
    ASSERT_TRUE(bodies && active);

    for (uint32_t i = 0; i < N; ++i) {
        active[i] = 1;
        if (i % 13 == 0) {
            make_static(&bodies[i]);
        } else if (i % 6 == 0) {
            make_sleeping(&bodies[i], 1.0f);
        } else {
            make_dynamic(&bodies[i], 1.0f);
        }
    }

    /* Sequential. */
    phys_frame_arena_t arena_seq;
    phys_frame_arena_init(&arena_seq, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_seq;
    memset(&lists_seq, 0, sizeof(lists_seq));

    phys_tier_classify_args_t args_seq = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_seq, .arena = &arena_seq,
    };
    phys_stage_tier_classify(&args_seq);

    /* Parallel. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena_par;
    phys_frame_arena_init(&arena_par, TEST_ARENA_SIZE);
    phys_tier_lists_t lists_par;
    memset(&lists_par, 0, sizeof(lists_par));

    phys_tier_classify_args_t args_par = {
        .bodies = bodies, .active = active, .body_count = N,
        .game = NULL, .tier_lists_out = &lists_par, .arena = &arena_par,
    };
    phys_stage_tier_classify_par(&args_par, &ctx);

    int cmp = compare_tier_lists(&lists_seq, &lists_par);
    ASSERT_TRUE(cmp == 0);

    /* Sanity: expect ceil(5000/1024) = 5 batches. */
    uint32_t expected_batches = (N + 1023) / 1024;
    ASSERT_EQ_UINT(5, expected_batches);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    phys_frame_arena_destroy(&arena_par);
    phys_frame_arena_destroy(&arena_seq);
    free(bodies);
    free(active);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_tier_identical_to_seq",  test_par_tier_identical_to_seq},
    {"par_tier_single_batch",      test_par_tier_single_batch},
    {"par_tier_multiple_batches",  test_par_tier_multiple_batches},
    {"par_tier_zero_bodies",       test_par_tier_zero_bodies},
    {"par_tier_all_static",        test_par_tier_all_static},
    {"par_tier_scales_linearly",   test_par_tier_scales_linearly},
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
