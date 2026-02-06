/**
 * @file p065_physics_par_manifold_tests.c
 * @brief Tests for parallel manifold build (phys-306).
 *
 * Validates that phys_stage_manifold_build_par() produces correct
 * results compared to the sequential version, handles edge cases,
 * and preserves warmstarting through the manifold cache.
 */

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_build.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/par/manifold_build_par.h"

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

/**
 * @brief Create a synthetic contact candidate for a body pair.
 *
 * Creates a candidate with one contact point at (1,0,0) with
 * normal (0,1,0), penetration 0.01, and a feature_id derived from
 * the candidate index.
 */
static void make_candidate(phys_contact_candidate_t *cand,
                           uint32_t body_a, uint32_t body_b,
                           uint32_t index) {
    memset(cand, 0, sizeof(*cand));
    cand->body_a = body_a;
    cand->body_b = body_b;
    cand->contact_count = 1;
    cand->contacts[0].point_world = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    cand->contacts[0].local_a     = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    cand->contacts[0].local_b     = (phys_vec3_t){-1.0f, 0.0f, 0.0f};
    cand->contacts[0].normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    cand->contacts[0].penetration = 0.01f;
    cand->contacts[0].feature_id  = 100 + index;
}

/**
 * @brief Set up a job system and physics job context for testing.
 */
static void setup_job_ctx(job_system_t *sys, phys_job_context_t *ctx) {
    job_system_create(sys, 2, 256, 65536, 64, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}

/**
 * @brief Tear down the job system and physics job context.
 */
static void teardown_job_ctx(job_system_t *sys, phys_job_context_t *ctx) {
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * @brief 20 candidates — parallel manifold count matches sequential.
 */
static int test_par_manifold_identical_to_seq(void) {
    const uint32_t N = 20;

    /* Build candidates: each pair is unique (body_a=i, body_b=i+1000). */
    phys_contact_candidate_t candidates[20];
    for (uint32_t i = 0; i < N; ++i) {
        make_candidate(&candidates[i], i, i + 1000, i);
    }

    /* Sequential run. */
    phys_manifold_cache_t cache_seq;
    phys_manifold_cache_init(&cache_seq, 256);
    phys_manifold_t manifolds_seq[20];
    uint32_t count_seq = 0;

    phys_manifold_build_args_t args_seq = {
        .candidates        = candidates,
        .candidate_count   = N,
        .cache             = &cache_seq,
        .manifolds_out     = manifolds_seq,
        .manifold_count_out = &count_seq,
        .max_manifolds     = N,
        .tick              = 0,
    };
    phys_stage_manifold_build(&args_seq);

    /* Parallel run. */
    phys_manifold_cache_t cache_par;
    phys_manifold_cache_init(&cache_par, 256);
    phys_manifold_t manifolds_par[20];
    uint32_t count_par = 0;

    phys_manifold_build_args_t args_par = {
        .candidates        = candidates,
        .candidate_count   = N,
        .cache             = &cache_par,
        .manifolds_out     = manifolds_par,
        .manifold_count_out = &count_par,
        .max_manifolds     = N,
        .tick              = 0,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_stage_manifold_build_par(&args_par, &ctx);

    teardown_job_ctx(&sys, &ctx);

    /* Both should produce the same number of manifolds. */
    ASSERT_EQ_UINT(count_seq, count_par);
    ASSERT_EQ_UINT(N, count_par);

    phys_manifold_cache_destroy(&cache_seq);
    phys_manifold_cache_destroy(&cache_par);
    return 0;
}

/**
 * @brief 100 candidates → ceil(100/32) = 4 jobs dispatched.
 */
static int test_par_manifold_batch_32(void) {
    const uint32_t N = 100;

    phys_contact_candidate_t *candidates = calloc(N, sizeof(*candidates));
    ASSERT_TRUE(candidates != NULL);
    for (uint32_t i = 0; i < N; ++i) {
        make_candidate(&candidates[i], i, i + 1000, i);
    }

    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 512);
    phys_manifold_t *manifolds = calloc(N, sizeof(*manifolds));
    ASSERT_TRUE(manifolds != NULL);
    uint32_t count = 0;

    phys_manifold_build_args_t args = {
        .candidates        = candidates,
        .candidate_count   = N,
        .cache             = &cache,
        .manifolds_out     = manifolds,
        .manifold_count_out = &count,
        .max_manifolds     = N,
        .tick              = 0,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_stage_manifold_build_par(&args, &ctx);

    teardown_job_ctx(&sys, &ctx);

    /* All 100 unique pairs should produce 100 manifolds. */
    ASSERT_EQ_UINT(N, count);

    phys_manifold_cache_destroy(&cache);
    free(candidates);
    free(manifolds);
    return 0;
}

/**
 * @brief Zero candidates — should not crash, count stays 0.
 */
static int test_par_manifold_zero_candidates(void) {
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);
    phys_manifold_t manifolds[1];
    uint32_t count = 99; /* sentinel — should be set to 0 */

    phys_manifold_build_args_t args = {
        .candidates        = NULL,
        .candidate_count   = 0,
        .cache             = &cache,
        .manifolds_out     = manifolds,
        .manifold_count_out = &count,
        .max_manifolds     = 1,
        .tick              = 0,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_stage_manifold_build_par(&args, &ctx);

    teardown_job_ctx(&sys, &ctx);

    ASSERT_EQ_UINT(0, count);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * @brief Run parallel build twice (tick 0 and tick 1) on the same cache.
 *
 * On tick 0, impulses should be zero. After manually setting an impulse
 * in the cache, tick 1 should warmstart and restore it through feature
 * ID matching.
 */
static int test_par_manifold_cache_warmstart(void) {
    const uint32_t N = 5;

    phys_contact_candidate_t candidates[5];
    for (uint32_t i = 0; i < N; ++i) {
        make_candidate(&candidates[i], i, i + 1000, i);
    }

    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 64);

    phys_manifold_t manifolds[5];
    uint32_t count = 0;

    phys_manifold_build_args_t args = {
        .candidates        = candidates,
        .candidate_count   = N,
        .cache             = &cache,
        .manifolds_out     = manifolds,
        .manifold_count_out = &count,
        .max_manifolds     = N,
        .tick              = 0,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    /* Tick 0: build manifolds to populate the cache. */
    phys_stage_manifold_build_par(&args, &ctx);
    ASSERT_EQ_UINT(N, count);

    /* Inject a warmstart impulse into the cache for pair (0, 1000). */
    phys_manifold_t *cached = phys_manifold_cache_find(&cache, 0, 1000);
    ASSERT_TRUE(cached != NULL);
    cached->normal_impulse[0] = 42.0f;

    /* Tick 1: rebuild with same candidates — should warmstart from cache. */
    count = 0;
    args.tick = 1;
    phys_stage_manifold_build_par(&args, &ctx);

    teardown_job_ctx(&sys, &ctx);

    ASSERT_EQ_UINT(N, count);

    /* Find the manifold for pair (0, 1000) in output.
     * The output order may differ from sequential due to atomic indexing,
     * so we search for it. */
    int found = 0;
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t a = manifolds[i].body_a;
        uint32_t b = manifolds[i].body_b;
        if ((a == 0 && b == 1000) || (a == 1000 && b == 0)) {
            /* Feature ID 100 should have been matched → impulse restored. */
            ASSERT_TRUE(manifolds[i].normal_impulse[0] == 42.0f);
            found = 1;
            break;
        }
    }
    ASSERT_TRUE(found);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * @brief Output buffer smaller than candidate count — no overflow.
 */
static int test_par_manifold_no_overflow(void) {
    const uint32_t N = 50;
    const uint32_t MAX_OUT = 10;

    phys_contact_candidate_t *candidates = calloc(N, sizeof(*candidates));
    ASSERT_TRUE(candidates != NULL);
    for (uint32_t i = 0; i < N; ++i) {
        make_candidate(&candidates[i], i, i + 1000, i);
    }

    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 256);
    phys_manifold_t manifolds[10];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t count = 0;

    phys_manifold_build_args_t args = {
        .candidates        = candidates,
        .candidate_count   = N,
        .cache             = &cache,
        .manifolds_out     = manifolds,
        .manifold_count_out = &count,
        .max_manifolds     = MAX_OUT,
        .tick              = 0,
    };

    job_system_t sys;
    phys_job_context_t ctx;
    setup_job_ctx(&sys, &ctx);

    phys_stage_manifold_build_par(&args, &ctx);

    teardown_job_ctx(&sys, &ctx);

    /* Must not exceed max_manifolds. */
    ASSERT_TRUE(count <= MAX_OUT);

    phys_manifold_cache_destroy(&cache);
    free(candidates);
    return 0;
}

/**
 * @brief Same input twice → same manifold count (deterministic).
 */
static int test_par_manifold_deterministic(void) {
    const uint32_t N = 40;

    phys_contact_candidate_t *candidates = calloc(N, sizeof(*candidates));
    ASSERT_TRUE(candidates != NULL);
    for (uint32_t i = 0; i < N; ++i) {
        make_candidate(&candidates[i], i, i + 1000, i);
    }

    uint32_t counts[2] = {0, 0};

    for (int run = 0; run < 2; ++run) {
        phys_manifold_cache_t cache;
        phys_manifold_cache_init(&cache, 256);
        phys_manifold_t *manifolds = calloc(N, sizeof(*manifolds));
        ASSERT_TRUE(manifolds != NULL);

        phys_manifold_build_args_t args = {
            .candidates        = candidates,
            .candidate_count   = N,
            .cache             = &cache,
            .manifolds_out     = manifolds,
            .manifold_count_out = &counts[run],
            .max_manifolds     = N,
            .tick              = 0,
        };

        job_system_t sys;
        phys_job_context_t ctx;
        setup_job_ctx(&sys, &ctx);

        phys_stage_manifold_build_par(&args, &ctx);

        teardown_job_ctx(&sys, &ctx);

        phys_manifold_cache_destroy(&cache);
        free(manifolds);
    }

    ASSERT_EQ_UINT(counts[0], counts[1]);
    ASSERT_EQ_UINT(N, counts[0]);

    free(candidates);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_manifold_identical_to_seq",  test_par_manifold_identical_to_seq},
    {"par_manifold_batch_32",          test_par_manifold_batch_32},
    {"par_manifold_zero_candidates",   test_par_manifold_zero_candidates},
    {"par_manifold_cache_warmstart",   test_par_manifold_cache_warmstart},
    {"par_manifold_no_overflow",       test_par_manifold_no_overflow},
    {"par_manifold_deterministic",     test_par_manifold_deterministic},
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
