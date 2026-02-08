/**
 * @file p048_physics_cache_commit_tests.c
 * @brief Unit tests for Stage 13: Cache Commit + Events.
 *
 * Tests cover: warmstart writeback, impact event emission, threshold
 * filtering, multiple constraints, max-event capping, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/cache_commit.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_cache.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Create a constraint with specified body pair, manifold/point index,
 *        and lambda values for the 3 rows (normal, tangent0, tangent1).
 */
static phys_constraint_t make_constraint(uint32_t body_a, uint32_t body_b,
                                         uint32_t manifold_idx,
                                         uint8_t point_idx,
                                         float normal_lambda,
                                         float tangent0_lambda,
                                         float tangent1_lambda)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.body_a       = body_a;
    c.body_b       = body_b;
    c.manifold_idx = manifold_idx;
    c.point_idx    = point_idx;
    c.row_count    = 3;
    c.rows[0].lambda = normal_lambda;
    c.rows[1].lambda = tangent0_lambda;
    c.rows[2].lambda = tangent1_lambda;
    return c;
}

/**
 * @brief Create a manifold with 1 contact point at the given position/normal.
 */
static phys_manifold_t make_manifold(uint32_t body_a, uint32_t body_b,
                                     float px, float py, float pz,
                                     float nx, float ny, float nz)
{
    phys_manifold_t m;
    memset(&m, 0, sizeof(m));
    m.body_a      = body_a;
    m.body_b      = body_b;
    m.point_count = 1;
    m.points[0].point_world = (phys_vec3_t){px, py, pz};
    m.points[0].normal      = (phys_vec3_t){nx, ny, nz};
    return m;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Solved impulses are written back to cache for warmstarting.
 */
static int test_cache_commit_warmstart(void)
{
    /* Set up cache with one entry for body pair (1, 2). */
    phys_manifold_cache_t cache;
    ASSERT_TRUE(phys_manifold_cache_init(&cache, 16) == 0);

    phys_manifold_t *cached = phys_manifold_cache_get_or_create(
        &cache, 1, 2, /*tick=*/0);
    ASSERT_TRUE(cached != NULL);
    cached->point_count = 1;

    /* Build source manifold and constraint with known lambdas. */
    phys_manifold_t manifolds[1];
    manifolds[0] = make_manifold(1, 2, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    phys_constraint_t constraints[1];
    constraints[0] = make_constraint(1, 2, 0, 0,
                                     5.0f,   /* normal lambda */
                                     1.5f,   /* tangent0 lambda */
                                     -0.3f); /* tangent1 lambda */

    phys_impact_event_t events[4];
    uint32_t event_count = 0;

    phys_cache_commit_args_t args = {
        .manifolds        = manifolds,
        .constraints      = constraints,
        .constraint_count = 1,
        .cache            = &cache,
        .events_out       = events,
        .event_count_out  = &event_count,
        .max_events       = 4,
        .impact_threshold = 100.0f, /* high threshold: no events expected */
        .warmstart_decay  = 0.85f,
    };

    phys_stage_cache_commit(&args);

    /* Verify cache has updated impulses (decayed by 0.85). */
    phys_manifold_t *result = phys_manifold_cache_find(&cache, 1, 2);
    ASSERT_TRUE(result != NULL);
    ASSERT_FLOAT_NEAR(5.0f  * 0.85f, result->normal_impulse[0],     1e-5f);
    ASSERT_FLOAT_NEAR(1.5f  * 0.85f, result->tangent_impulse[0][0], 1e-5f);
    ASSERT_FLOAT_NEAR(-0.3f * 0.85f, result->tangent_impulse[0][1], 1e-5f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 2: Constraint with large normal impulse emits correct impact event.
 */
static int test_cache_commit_impact_event(void)
{
    phys_manifold_cache_t cache;
    ASSERT_TRUE(phys_manifold_cache_init(&cache, 16) == 0);

    phys_manifold_t *cached = phys_manifold_cache_get_or_create(
        &cache, 3, 4, 0);
    ASSERT_TRUE(cached != NULL);
    cached->point_count = 1;

    phys_manifold_t manifolds[1];
    manifolds[0] = make_manifold(3, 4, 1.0f, 2.0f, 3.0f, 0.0f, 1.0f, 0.0f);

    phys_constraint_t constraints[1];
    constraints[0] = make_constraint(3, 4, 0, 0, 25.0f, 0.5f, -0.5f);

    phys_impact_event_t events[4];
    uint32_t event_count = 0;

    phys_cache_commit_args_t args = {
        .manifolds        = manifolds,
        .constraints      = constraints,
        .constraint_count = 1,
        .cache            = &cache,
        .events_out       = events,
        .event_count_out  = &event_count,
        .max_events       = 4,
        .impact_threshold = 10.0f,
        .warmstart_decay  = 1.0f,
    };

    phys_stage_cache_commit(&args);

    /* One event expected. */
    ASSERT_TRUE(event_count == 1);
    ASSERT_TRUE(events[0].body_a == 3);
    ASSERT_TRUE(events[0].body_b == 4);
    ASSERT_FLOAT_NEAR(1.0f, events[0].point.x, 1e-5f);
    ASSERT_FLOAT_NEAR(2.0f, events[0].point.y, 1e-5f);
    ASSERT_FLOAT_NEAR(3.0f, events[0].point.z, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, events[0].normal.x, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, events[0].normal.y, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, events[0].normal.z, 1e-5f);
    ASSERT_FLOAT_NEAR(25.0f, events[0].impulse_magnitude, 1e-5f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 3: Constraint with impulse below threshold emits no event.
 */
static int test_cache_commit_threshold_filter(void)
{
    phys_manifold_cache_t cache;
    ASSERT_TRUE(phys_manifold_cache_init(&cache, 16) == 0);

    phys_manifold_t *cached = phys_manifold_cache_get_or_create(
        &cache, 5, 6, 0);
    ASSERT_TRUE(cached != NULL);
    cached->point_count = 1;

    phys_manifold_t manifolds[1];
    manifolds[0] = make_manifold(5, 6, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    /* Impulse of 2.0 is below threshold of 10.0. */
    phys_constraint_t constraints[1];
    constraints[0] = make_constraint(5, 6, 0, 0, 2.0f, 0.1f, 0.0f);

    phys_impact_event_t events[4];
    uint32_t event_count = 99; /* sentinel to verify it gets set to 0 */

    phys_cache_commit_args_t args = {
        .manifolds        = manifolds,
        .constraints      = constraints,
        .constraint_count = 1,
        .cache            = &cache,
        .events_out       = events,
        .event_count_out  = &event_count,
        .max_events       = 4,
        .impact_threshold = 10.0f,
        .warmstart_decay  = 1.0f,
    };

    phys_stage_cache_commit(&args);

    ASSERT_TRUE(event_count == 0);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 4: 3 constraints, 2 above threshold → 2 events emitted.
 */
static int test_cache_commit_multiple_constraints(void)
{
    phys_manifold_cache_t cache;
    ASSERT_TRUE(phys_manifold_cache_init(&cache, 16) == 0);

    /* Create 3 cache entries. */
    phys_manifold_t *c0 = phys_manifold_cache_get_or_create(&cache, 1, 2, 0);
    phys_manifold_t *c1 = phys_manifold_cache_get_or_create(&cache, 3, 4, 0);
    phys_manifold_t *c2 = phys_manifold_cache_get_or_create(&cache, 5, 6, 0);
    ASSERT_TRUE(c0 && c1 && c2);
    c0->point_count = 1;
    c1->point_count = 1;
    c2->point_count = 1;

    phys_manifold_t manifolds[3];
    manifolds[0] = make_manifold(1, 2, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    manifolds[1] = make_manifold(3, 4, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    manifolds[2] = make_manifold(5, 6, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    phys_constraint_t constraints[3];
    constraints[0] = make_constraint(1, 2, 0, 0, 20.0f, 0.0f, 0.0f); /* above */
    constraints[1] = make_constraint(3, 4, 1, 0,  3.0f, 0.0f, 0.0f); /* below */
    constraints[2] = make_constraint(5, 6, 2, 0, 15.0f, 0.0f, 0.0f); /* above */

    phys_impact_event_t events[8];
    uint32_t event_count = 0;

    phys_cache_commit_args_t args = {
        .manifolds        = manifolds,
        .constraints      = constraints,
        .constraint_count = 3,
        .cache            = &cache,
        .events_out       = events,
        .event_count_out  = &event_count,
        .max_events       = 8,
        .impact_threshold = 10.0f,
        .warmstart_decay  = 1.0f,
    };

    phys_stage_cache_commit(&args);

    ASSERT_TRUE(event_count == 2);
    /* First event from constraint 0. */
    ASSERT_TRUE(events[0].body_a == 1);
    ASSERT_TRUE(events[0].body_b == 2);
    ASSERT_FLOAT_NEAR(20.0f, events[0].impulse_magnitude, 1e-5f);
    /* Second event from constraint 2. */
    ASSERT_TRUE(events[1].body_a == 5);
    ASSERT_TRUE(events[1].body_b == 6);
    ASSERT_FLOAT_NEAR(15.0f, events[1].impulse_magnitude, 1e-5f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 5: More events than max_events → output is capped.
 */
static int test_cache_commit_max_events(void)
{
    phys_manifold_cache_t cache;
    ASSERT_TRUE(phys_manifold_cache_init(&cache, 16) == 0);

    /* Create 3 cache entries, all above threshold. */
    phys_manifold_t *c0 = phys_manifold_cache_get_or_create(&cache, 1, 2, 0);
    phys_manifold_t *c1 = phys_manifold_cache_get_or_create(&cache, 3, 4, 0);
    phys_manifold_t *c2 = phys_manifold_cache_get_or_create(&cache, 5, 6, 0);
    ASSERT_TRUE(c0 && c1 && c2);
    c0->point_count = 1;
    c1->point_count = 1;
    c2->point_count = 1;

    phys_manifold_t manifolds[3];
    manifolds[0] = make_manifold(1, 2, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    manifolds[1] = make_manifold(3, 4, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);
    manifolds[2] = make_manifold(5, 6, 2.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f);

    phys_constraint_t constraints[3];
    constraints[0] = make_constraint(1, 2, 0, 0, 50.0f, 0.0f, 0.0f);
    constraints[1] = make_constraint(3, 4, 1, 0, 40.0f, 0.0f, 0.0f);
    constraints[2] = make_constraint(5, 6, 2, 0, 30.0f, 0.0f, 0.0f);

    /* Only allow 2 events. */
    phys_impact_event_t events[2];
    uint32_t event_count = 0;

    phys_cache_commit_args_t args = {
        .manifolds        = manifolds,
        .constraints      = constraints,
        .constraint_count = 3,
        .cache            = &cache,
        .events_out       = events,
        .event_count_out  = &event_count,
        .max_events       = 2,
        .impact_threshold = 5.0f,
        .warmstart_decay  = 1.0f,
    };

    phys_stage_cache_commit(&args);

    /* Capped at max_events=2 even though all 3 are above threshold. */
    ASSERT_TRUE(event_count == 2);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 6: NULL args does not crash.
 */
static int test_cache_commit_null_safe(void)
{
    phys_stage_cache_commit(NULL);
    /* If we get here without crashing, the test passes. */
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p048_physics_cache_commit_tests:\n");

    RUN_TEST(test_cache_commit_warmstart);
    RUN_TEST(test_cache_commit_impact_event);
    RUN_TEST(test_cache_commit_threshold_filter);
    RUN_TEST(test_cache_commit_multiple_constraints);
    RUN_TEST(test_cache_commit_max_events);
    RUN_TEST(test_cache_commit_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
