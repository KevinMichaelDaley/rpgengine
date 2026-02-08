/**
 * @file p060_physics_job_infra_tests.c
 * @brief Tests for the physics job infrastructure (phys-301).
 *
 * Validates phys_job_context_t init/destroy, stage dispatch with batching,
 * zero-item dispatch, batch start/count layout, multi-stage independence,
 * and Tracy-named dispatch.
 */

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/job/counter.h"
#include "ferrum/physics/phys_jobs.h"

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

/* ── Shared job functions ───────────────────────────────────────── */

/**
 * @brief Job function that atomically adds batch.count to a shared counter.
 *
 * Expects batch.user_args to point to an atomic_uint.
 */
static void count_job(void *data) {
    phys_job_batch_t *b = data;
    atomic_uint *counter = b->user_args;
    atomic_fetch_add(counter, b->count);
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_context_init_destroy(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    ASSERT_TRUE(ctx.job_sys == &sys);
    phys_job_context_destroy(&ctx);

    job_system_shutdown(&sys);
    return 0;
}

static int test_dispatch_single_batch(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    atomic_uint shared_counter = 0;
    phys_job_batch_t batches[1];

    uint32_t num = phys_dispatch_stage(&ctx, PHYS_STAGE_BROADPHASE,
                                       count_job, &shared_counter,
                                       10, 10, batches);
    /* Single batch runs inline (no fiber dispatch), returns 0. */
    ASSERT_EQ_UINT(0, num);

    /* Wait is a no-op since work ran inline, but calling it must not hang. */
    phys_wait_stage(&ctx, PHYS_STAGE_BROADPHASE);
    ASSERT_EQ_UINT(10, atomic_load(&shared_counter));

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

static int test_dispatch_multiple_batches(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    atomic_uint shared_counter = 0;
    phys_job_batch_t batches[4];

    uint32_t num = phys_dispatch_stage(&ctx, PHYS_STAGE_NARROWPHASE,
                                       count_job, &shared_counter,
                                       100, 32, batches);
    ASSERT_EQ_UINT(4, num);

    phys_wait_stage(&ctx, PHYS_STAGE_NARROWPHASE);
    ASSERT_EQ_UINT(100, atomic_load(&shared_counter));

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

static int test_dispatch_zero_items(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_job_batch_t batches[1];
    uint32_t num = phys_dispatch_stage(&ctx, PHYS_STAGE_TGS_SOLVE,
                                       count_job, NULL,
                                       0, 32, batches);
    ASSERT_EQ_UINT(0, num);

    /* Wait should be a no-op and not hang. */
    phys_wait_stage(&ctx, PHYS_STAGE_TGS_SOLVE);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

static int test_batch_start_count(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    atomic_uint dummy = 0;
    phys_job_batch_t batches[3];

    uint32_t num = phys_dispatch_stage(&ctx, PHYS_STAGE_INTEGRATE,
                                       count_job, &dummy,
                                       7, 3, batches);
    ASSERT_EQ_UINT(3, num);

    /* Verify batch layout before jobs run. */
    ASSERT_EQ_UINT(0, batches[0].start);
    ASSERT_EQ_UINT(3, batches[0].count);
    ASSERT_EQ_UINT(3, batches[1].start);
    ASSERT_EQ_UINT(3, batches[1].count);
    ASSERT_EQ_UINT(6, batches[2].start);
    ASSERT_EQ_UINT(1, batches[2].count);

    phys_wait_stage(&ctx, PHYS_STAGE_INTEGRATE);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

static int test_multiple_stages(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    atomic_uint counter_a = 0;
    atomic_uint counter_b = 0;
    phys_job_batch_t batches_a[1];
    phys_job_batch_t batches_b[1];

    phys_dispatch_stage(&ctx, PHYS_STAGE_TIER_CLASSIFY,
                        count_job, &counter_a, 5, 5, batches_a);
    phys_dispatch_stage(&ctx, PHYS_STAGE_BROADPHASE,
                        count_job, &counter_b, 8, 8, batches_b);

    phys_wait_stage(&ctx, PHYS_STAGE_TIER_CLASSIFY);
    phys_wait_stage(&ctx, PHYS_STAGE_BROADPHASE);

    ASSERT_EQ_UINT(5, atomic_load(&counter_a));
    ASSERT_EQ_UINT(8, atomic_load(&counter_b));

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

static int test_dispatch_names_tracy(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    atomic_uint dummy = 0;
    phys_job_batch_t batches[1];

    /* Dispatch with a named stage — verifies no crash from debug_name path.
       Single batch runs inline, returning 0. */
    uint32_t num = phys_dispatch_stage(&ctx, PHYS_STAGE_STEP_PLAN,
                                       count_job, &dummy,
                                       1, 1, batches);
    ASSERT_EQ_UINT(0, num);
    phys_wait_stage(&ctx, PHYS_STAGE_STEP_PLAN);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"context_init_destroy",     test_context_init_destroy},
    {"dispatch_single_batch",    test_dispatch_single_batch},
    {"dispatch_multiple_batches", test_dispatch_multiple_batches},
    {"dispatch_zero_items",      test_dispatch_zero_items},
    {"batch_start_count",        test_batch_start_count},
    {"multiple_stages",          test_multiple_stages},
    {"dispatch_names_tracy",     test_dispatch_names_tracy},
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
