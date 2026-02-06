/**
 * @file p040_physics_manifold_build_tests.c
 * @brief Unit tests for Stage 7: Manifold Build + Cache Merge.
 *
 * Tests cover: new contact creation, warmstart matching, warmstart
 * mismatch, multiple candidates, max-points cap, cache update
 * verification, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_build.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/narrowphase.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                   \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: "                    \
                    "expected %d got %d\n",                                     \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
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
 * @brief Create a single contact point with the given feature ID.
 */
static phys_contact_point_t make_point(uint32_t feature_id, float pen)
{
    phys_contact_point_t p;
    memset(&p, 0, sizeof(p));
    p.feature_id  = feature_id;
    p.penetration = pen;
    p.normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    return p;
}

/**
 * @brief Create a contact candidate with a single contact point.
 */
static phys_contact_candidate_t make_candidate(uint32_t body_a,
                                                uint32_t body_b,
                                                uint32_t feature_id,
                                                float pen)
{
    phys_contact_candidate_t c;
    memset(&c, 0, sizeof(c));
    c.body_a        = body_a;
    c.body_b        = body_b;
    c.contact_count = 1;
    c.contacts[0]   = make_point(feature_id, pen);
    return c;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: A brand-new contact (no cache entry) creates a manifold
 * with zero warmstart impulses.
 */
static int test_manifold_build_new_contact(void)
{
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 16);

    phys_contact_candidate_t cand = make_candidate(1, 2, 100, 0.05f);

    phys_manifold_t out[4];
    uint32_t count = 0;

    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.candidates        = &cand;
    args.candidate_count   = 1;
    args.cache             = &cache;
    args.manifolds_out     = out;
    args.manifold_count_out = &count;
    args.max_manifolds     = 4;
    args.tick              = 1;

    phys_stage_manifold_build(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(1, (int)out[0].point_count);
    ASSERT_INT_EQ(100, (int)out[0].points[0].feature_id);

    /* Warmstart impulses should be zero for a new contact. */
    ASSERT_FLOAT_NEAR(0.0f, out[0].normal_impulse[0], 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, out[0].tangent_impulse[0][0], 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, out[0].tangent_impulse[0][1], 1e-6f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 2: Pre-populate cache with impulses, then build with the same
 * feature_id → impulses are preserved (warmstart match).
 */
static int test_manifold_build_warmstart_match(void)
{
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 16);

    /* Seed the cache at tick 0 with known impulses. */
    phys_manifold_t *m = phys_manifold_cache_get_or_create(&cache, 1, 2, 0);
    ASSERT_TRUE(m != NULL);

    phys_contact_point_t pt = make_point(42, 0.1f);
    phys_manifold_add_point(m, &pt);
    m->normal_impulse[0]     = 5.0f;
    m->tangent_impulse[0][0] = 1.5f;
    m->tangent_impulse[0][1] = 2.5f;

    /* Now run the build stage with the same feature_id at tick 1. */
    phys_contact_candidate_t cand = make_candidate(1, 2, 42, 0.08f);

    phys_manifold_t out[4];
    uint32_t count = 0;

    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.candidates        = &cand;
    args.candidate_count   = 1;
    args.cache             = &cache;
    args.manifolds_out     = out;
    args.manifold_count_out = &count;
    args.max_manifolds     = 4;
    args.tick              = 1;

    phys_stage_manifold_build(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(1, (int)out[0].point_count);

    /* Impulses from the old cache entry should be restored. */
    ASSERT_FLOAT_NEAR(5.0f, out[0].normal_impulse[0], 1e-6f);
    ASSERT_FLOAT_NEAR(1.5f, out[0].tangent_impulse[0][0], 1e-6f);
    ASSERT_FLOAT_NEAR(2.5f, out[0].tangent_impulse[0][1], 1e-6f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 3: Pre-populate cache with impulses, build with a *different*
 * feature_id → impulses are zero (warmstart mismatch).
 */
static int test_manifold_build_warmstart_no_match(void)
{
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 16);

    /* Seed the cache with feature_id 42 and non-zero impulses. */
    phys_manifold_t *m = phys_manifold_cache_get_or_create(&cache, 3, 4, 0);
    ASSERT_TRUE(m != NULL);

    phys_contact_point_t pt = make_point(42, 0.1f);
    phys_manifold_add_point(m, &pt);
    m->normal_impulse[0]     = 10.0f;
    m->tangent_impulse[0][0] = 3.0f;
    m->tangent_impulse[0][1] = 4.0f;

    /* Build with feature_id 99 — different from cached 42. */
    phys_contact_candidate_t cand = make_candidate(3, 4, 99, 0.05f);

    phys_manifold_t out[4];
    uint32_t count = 0;

    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.candidates        = &cand;
    args.candidate_count   = 1;
    args.cache             = &cache;
    args.manifolds_out     = out;
    args.manifold_count_out = &count;
    args.max_manifolds     = 4;
    args.tick              = 1;

    phys_stage_manifold_build(&args);

    ASSERT_INT_EQ(1, (int)count);

    /* No match → impulses should remain zero. */
    ASSERT_FLOAT_NEAR(0.0f, out[0].normal_impulse[0], 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, out[0].tangent_impulse[0][0], 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, out[0].tangent_impulse[0][1], 1e-6f);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 4: Three distinct candidates produce three manifolds.
 */
static int test_manifold_build_multiple_candidates(void)
{
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 16);

    phys_contact_candidate_t cands[3];
    cands[0] = make_candidate(0, 1, 10, 0.1f);
    cands[1] = make_candidate(2, 3, 20, 0.2f);
    cands[2] = make_candidate(4, 5, 30, 0.3f);

    phys_manifold_t out[8];
    uint32_t count = 0;

    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.candidates        = cands;
    args.candidate_count   = 3;
    args.cache             = &cache;
    args.manifolds_out     = out;
    args.manifold_count_out = &count;
    args.max_manifolds     = 8;
    args.tick              = 1;

    phys_stage_manifold_build(&args);

    ASSERT_INT_EQ(3, (int)count);

    /* Verify each manifold has correct body pair. */
    ASSERT_INT_EQ(0, (int)out[0].body_a);
    ASSERT_INT_EQ(1, (int)out[0].body_b);
    ASSERT_INT_EQ(2, (int)out[1].body_a);
    ASSERT_INT_EQ(3, (int)out[1].body_b);
    ASSERT_INT_EQ(4, (int)out[2].body_a);
    ASSERT_INT_EQ(5, (int)out[2].body_b);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 5: Fill a candidate with PHYS_MAX_MANIFOLD_POINTS contacts,
 * then run build again with 4 more for the same pair. The cached
 * manifold starts with 4 points from frame 1; frame 2 adds 4 new
 * points via add_point (which triggers reduction), keeping at most 4.
 */
static int test_manifold_build_max_points(void)
{
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 16);

    /* Frame 1: fill the manifold with 4 contacts. */
    phys_contact_candidate_t cand1;
    memset(&cand1, 0, sizeof(cand1));
    cand1.body_a        = 7;
    cand1.body_b        = 8;
    cand1.contact_count = PHYS_MAX_MANIFOLD_POINTS;
    for (uint8_t j = 0; j < PHYS_MAX_MANIFOLD_POINTS; ++j) {
        cand1.contacts[j] = make_point(j + 1, 0.01f * (float)(j + 1));
        cand1.contacts[j].point_world.x = (float)j * 2.0f;
        cand1.contacts[j].point_world.z = (float)(j % 2) * 2.0f;
    }

    phys_manifold_t out[4];
    uint32_t count = 0;

    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.candidates        = &cand1;
    args.candidate_count   = 1;
    args.cache             = &cache;
    args.manifolds_out     = out;
    args.manifold_count_out = &count;
    args.max_manifolds     = 4;
    args.tick              = 1;

    phys_stage_manifold_build(&args);

    ASSERT_INT_EQ(1, (int)count);
    ASSERT_INT_EQ(PHYS_MAX_MANIFOLD_POINTS, (int)out[0].point_count);

    /* Frame 2: same pair with 4 new contacts — still capped at 4. */
    phys_contact_candidate_t cand2;
    memset(&cand2, 0, sizeof(cand2));
    cand2.body_a        = 7;
    cand2.body_b        = 8;
    cand2.contact_count = PHYS_MAX_MANIFOLD_POINTS;
    for (uint8_t j = 0; j < PHYS_MAX_MANIFOLD_POINTS; ++j) {
        cand2.contacts[j] = make_point(j + 10, 0.02f * (float)(j + 1));
        cand2.contacts[j].point_world.x = (float)j * 3.0f;
        cand2.contacts[j].point_world.z = (float)(j % 2) * 3.0f;
    }

    count = 0;
    args.candidates      = &cand2;
    args.candidate_count = 1;
    args.tick            = 2;

    phys_stage_manifold_build(&args);

    ASSERT_INT_EQ(1, (int)count);
    /* Still capped at PHYS_MAX_MANIFOLD_POINTS. */
    ASSERT_TRUE(out[0].point_count <= PHYS_MAX_MANIFOLD_POINTS);
    ASSERT_INT_EQ(PHYS_MAX_MANIFOLD_POINTS, (int)out[0].point_count);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 6: After build, the cache entry reflects the new contacts.
 */
static int test_manifold_build_cache_updated(void)
{
    phys_manifold_cache_t cache;
    phys_manifold_cache_init(&cache, 16);

    phys_contact_candidate_t cand = make_candidate(10, 11, 77, 0.03f);

    phys_manifold_t out[4];
    uint32_t count = 0;

    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.candidates        = &cand;
    args.candidate_count   = 1;
    args.cache             = &cache;
    args.manifolds_out     = out;
    args.manifold_count_out = &count;
    args.max_manifolds     = 4;
    args.tick              = 5;

    phys_stage_manifold_build(&args);

    /* The cache should now contain an entry for (10, 11). */
    phys_manifold_t *cached = phys_manifold_cache_find(&cache, 10, 11);
    ASSERT_TRUE(cached != NULL);
    ASSERT_INT_EQ(1, (int)cached->point_count);
    ASSERT_INT_EQ(77, (int)cached->points[0].feature_id);

    phys_manifold_cache_destroy(&cache);
    return 0;
}

/**
 * Test 7: NULL args doesn't crash.
 */
static int test_manifold_build_null_safe(void)
{
    /* NULL args — should be a safe no-op. */
    phys_stage_manifold_build(NULL);

    /* Args with NULL internals. */
    phys_manifold_build_args_t args;
    memset(&args, 0, sizeof(args));
    uint32_t count = 99;
    args.manifold_count_out = &count;
    phys_stage_manifold_build(&args);
    ASSERT_INT_EQ(0, (int)count);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p040_physics_manifold_build_tests\n");

    RUN_TEST(test_manifold_build_new_contact);
    RUN_TEST(test_manifold_build_warmstart_match);
    RUN_TEST(test_manifold_build_warmstart_no_match);
    RUN_TEST(test_manifold_build_multiple_candidates);
    RUN_TEST(test_manifold_build_max_points);
    RUN_TEST(test_manifold_build_cache_updated);
    RUN_TEST(test_manifold_build_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
