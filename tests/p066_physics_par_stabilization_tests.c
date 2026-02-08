/**
 * @file p066_physics_par_stabilization_tests.c
 * @brief Tests for parallel stabilization hint computation (phys-307).
 *
 * Validates that phys_stage_stabilization_par produces identical hints
 * to phys_stage_stabilization across various manifold counts and contact
 * scenarios.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/par/stabilization_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/stabilization.h"

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

#define ASSERT_FLOAT_EQ(expected, actual, eps)                                 \
    do {                                                                        \
        float _exp = (float)(expected);                                        \
        float _act = (float)(actual);                                          \
        if (fabsf(_exp - _act) > (eps)) {                                      \
            TEST_FAIL("expected %.6f got %.6f", (double)_exp, (double)_act);   \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ── Helpers ────────────────────────────────────────────────────── */

/** Resting velocity threshold used across all tests. */
#define TEST_THRESHOLD 0.5f

/**
 * @brief Build a manifold between two body indices with a single
 *        contact point at the given world position and normal.
 */
static void build_manifold(phys_manifold_t *m,
                           uint32_t body_a, uint32_t body_b,
                           float px, float py, float pz,
                           float nx, float ny, float nz) {
    phys_manifold_init(m, body_a, body_b);
    phys_contact_point_t cp;
    memset(&cp, 0, sizeof(cp));
    cp.point_world = (phys_vec3_t){px, py, pz};
    cp.normal      = (phys_vec3_t){nx, ny, nz};
    cp.penetration = 0.01f;
    cp.feature_id  = 0;
    phys_manifold_add_point(m, &cp);
}

/**
 * @brief Create a dynamic body at the given position with linear velocity.
 */
static void make_body(phys_body_t *body,
                      float px, float py, float pz,
                      float vx, float vy, float vz) {
    phys_body_init(body);
    phys_body_set_mass(body, 1.0f);
    body->position   = (phys_vec3_t){px, py, pz};
    body->linear_vel = (phys_vec3_t){vx, vy, vz};
    body->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Compare two hint arrays for exact equality.
 * @return 0 if identical, 1 if different.
 */
static int compare_hints(const phys_stab_hint_t *a,
                         const phys_stab_hint_t *b,
                         uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (a[i].friction_scale != b[i].friction_scale ||
            a[i].restitution_scale != b[i].restitution_scale) {
            fprintf(stderr,
                    "  hint[%u] mismatch: seq(%.3f,%.3f) par(%.3f,%.3f)\n",
                    i,
                    (double)a[i].friction_scale,
                    (double)a[i].restitution_scale,
                    (double)b[i].friction_scale,
                    (double)b[i].restitution_scale);
            return 1;
        }
    }
    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: 20 manifolds. Compare hints from sequential vs parallel.
 * Bodies have varying velocities so some contacts are resting, some active.
 */
static int test_par_stab_identical_to_seq(void) {
    const uint32_t N = 20;
    const uint32_t NBODIES = N * 2;

    phys_body_t *bodies = calloc(NBODIES, sizeof(phys_body_t));
    phys_manifold_t *manifolds = calloc(N, sizeof(phys_manifold_t));
    phys_stab_hint_t *hints_seq = calloc(N, sizeof(phys_stab_hint_t));
    phys_stab_hint_t *hints_par = calloc(N, sizeof(phys_stab_hint_t));
    ASSERT_TRUE(bodies && manifolds && hints_seq && hints_par);

    /* Set up body pairs with varying velocities. */
    for (uint32_t i = 0; i < N; ++i) {
        uint32_t a = i * 2;
        uint32_t b = i * 2 + 1;
        float speed = (float)i * 0.1f; /* 0.0 to 1.9 — some below threshold */
        make_body(&bodies[a], 0.0f, 0.0f, 0.0f, speed, 0.0f, 0.0f);
        make_body(&bodies[b], 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        build_manifold(&manifolds[i], a, b,
                       0.5f, 0.0f, 0.0f,  /* contact at midpoint */
                       1.0f, 0.0f, 0.0f); /* normal along X */
    }

    /* Sequential. */
    phys_stabilization_args_t args_seq = {
        .manifolds = manifolds,
        .manifold_count = N,
        .bodies = bodies,
        .hints_out = hints_seq,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_stage_stabilization(&args_seq);

    /* Parallel. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stabilization_args_t args_par = {
        .manifolds = manifolds,
        .manifold_count = N,
        .bodies = bodies,
        .hints_out = hints_par,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_frame_arena_reset(&arena);
    phys_stage_stabilization_par(&args_par, &ctx, &arena);

    /* Compare. */
    int cmp = compare_hints(hints_seq, hints_par, N);
    ASSERT_TRUE(cmp == 0);

    phys_frame_arena_destroy(&arena);
    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    free(hints_par);
    free(hints_seq);
    free(manifolds);
    free(bodies);
    return 0;
}

/**
 * Test 2: 200 manifolds → ceil(200/64) = 4 jobs. Results match sequential.
 */
static int test_par_stab_batch_64(void) {
    const uint32_t N = 200;
    const uint32_t NBODIES = N * 2;

    /* Verify expected batch count. */
    uint32_t expected_batches = (N + PHYS_STABILIZATION_BATCH_SIZE - 1)
                                / PHYS_STABILIZATION_BATCH_SIZE;
    ASSERT_EQ_UINT(4, expected_batches);

    phys_body_t *bodies = calloc(NBODIES, sizeof(phys_body_t));
    phys_manifold_t *manifolds = calloc(N, sizeof(phys_manifold_t));
    phys_stab_hint_t *hints_seq = calloc(N, sizeof(phys_stab_hint_t));
    phys_stab_hint_t *hints_par = calloc(N, sizeof(phys_stab_hint_t));
    ASSERT_TRUE(bodies && manifolds && hints_seq && hints_par);

    for (uint32_t i = 0; i < N; ++i) {
        uint32_t a = i * 2;
        uint32_t b = i * 2 + 1;
        float speed = (i % 10 < 5) ? 0.1f : 5.0f;
        make_body(&bodies[a], 0.0f, 0.0f, 0.0f, speed, 0.0f, 0.0f);
        make_body(&bodies[b], 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
        build_manifold(&manifolds[i], a, b,
                       0.5f, 0.0f, 0.0f,
                       1.0f, 0.0f, 0.0f);
    }

    /* Sequential. */
    phys_stabilization_args_t args_seq = {
        .manifolds = manifolds,
        .manifold_count = N,
        .bodies = bodies,
        .hints_out = hints_seq,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_stage_stabilization(&args_seq);

    /* Parallel. */
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stabilization_args_t args_par = {
        .manifolds = manifolds,
        .manifold_count = N,
        .bodies = bodies,
        .hints_out = hints_par,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_frame_arena_reset(&arena);
    phys_stage_stabilization_par(&args_par, &ctx, &arena);

    int cmp = compare_hints(hints_seq, hints_par, N);
    ASSERT_TRUE(cmp == 0);

    phys_frame_arena_destroy(&arena);
    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    free(hints_par);
    free(hints_seq);
    free(manifolds);
    free(bodies);
    return 0;
}

/**
 * Test 3: 0 manifolds → no crash, no jobs dispatched.
 */
static int test_par_stab_zero_manifolds(void) {
    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stabilization_args_t args = {
        .manifolds = NULL,
        .manifold_count = 0,
        .bodies = NULL,
        .hints_out = NULL,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    /* Should not crash. */
    phys_frame_arena_reset(&arena);
    phys_stage_stabilization_par(&args, &ctx, &arena);

    phys_frame_arena_destroy(&arena);
    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

/**
 * Test 4: 1 manifold → 1 job. Verify correct hint value.
 */
static int test_par_stab_single_manifold(void) {
    phys_body_t bodies[2];
    /* Both bodies at rest — resting contact expected. */
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    make_body(&bodies[1], 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    phys_manifold_t manifold;
    build_manifold(&manifold, 0, 1,
                   0.5f, 0.0f, 0.0f,
                   1.0f, 0.0f, 0.0f);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stabilization_args_t args = {
        .manifolds = &manifold,
        .manifold_count = 1,
        .bodies = bodies,
        .hints_out = &hint,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_frame_arena_reset(&arena);
    phys_stage_stabilization_par(&args, &ctx, &arena);

    /* Zero relative velocity → resting contact.
     * T0: base 3.0 × tier friction_boost 3.0 = 9.0 */
    ASSERT_FLOAT_EQ(9.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_EQ(0.0f, hint.restitution_scale, 1e-5f);

    phys_frame_arena_destroy(&arena);
    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

/**
 * Test 5: Manifold with low-velocity bodies → resting hint.
 * Both bodies move slowly (below threshold), so the relative velocity
 * at the contact point is below the resting threshold.
 */
static int test_par_stab_resting_contact(void) {
    phys_body_t bodies[2];
    /* Both bodies moving slowly in the same direction. */
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f, 0.1f, 0.0f, 0.0f);
    make_body(&bodies[1], 1.0f, 0.0f, 0.0f, 0.05f, 0.0f, 0.0f);

    phys_manifold_t manifold;
    build_manifold(&manifold, 0, 1,
                   0.5f, 0.0f, 0.0f,
                   1.0f, 0.0f, 0.0f);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stabilization_args_t args = {
        .manifolds = &manifold,
        .manifold_count = 1,
        .bodies = bodies,
        .hints_out = &hint,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_frame_arena_reset(&arena);
    phys_stage_stabilization_par(&args, &ctx, &arena);

    /* Relative velocity = 0.05 along normal, well below 0.5 threshold.
     * T0: base 3.0 × tier friction_boost 3.0 = 9.0 */
    ASSERT_FLOAT_EQ(9.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_EQ(0.0f, hint.restitution_scale, 1e-5f);

    phys_frame_arena_destroy(&arena);
    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);
    return 0;
}

/**
 * Test 6: Manifold with high-velocity bodies → active/impact hint.
 * Body A is moving fast toward body B, so relative velocity exceeds
 * the resting threshold.
 */
static int test_par_stab_fast_contact(void) {
    phys_body_t bodies[2];
    /* Body A approaching B at high speed along the contact normal. */
    make_body(&bodies[0], 0.0f, 0.0f, 0.0f, 10.0f, 0.0f, 0.0f);
    make_body(&bodies[1], 1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);

    phys_manifold_t manifold;
    build_manifold(&manifold, 0, 1,
                   0.5f, 0.0f, 0.0f,
                   1.0f, 0.0f, 0.0f);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stabilization_args_t args = {
        .manifolds = &manifold,
        .manifold_count = 1,
        .bodies = bodies,
        .hints_out = &hint,
        .resting_velocity_threshold = TEST_THRESHOLD,
    };
    phys_frame_arena_reset(&arena);
    phys_stage_stabilization_par(&args, &ctx, &arena);

    /* High relative velocity → active contact (pass-through). */
    ASSERT_FLOAT_EQ(1.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_EQ(1.0f, hint.restitution_scale, 1e-5f);

    phys_frame_arena_destroy(&arena);
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
    {"par_stab_identical_to_seq", test_par_stab_identical_to_seq},
    {"par_stab_batch_64",         test_par_stab_batch_64},
    {"par_stab_zero_manifolds",   test_par_stab_zero_manifolds},
    {"par_stab_single_manifold",  test_par_stab_single_manifold},
    {"par_stab_resting_contact",  test_par_stab_resting_contact},
    {"par_stab_fast_contact",     test_par_stab_fast_contact},
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
