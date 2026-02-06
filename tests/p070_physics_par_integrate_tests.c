/**
 * @file p070_physics_par_integrate_tests.c
 * @brief Tests for parallel integration (phys-310).
 *
 * Validates that phys_stage_integrate_par() produces identical results
 * to phys_stage_integrate(), handles edge cases correctly, and properly
 * detects sleeping bodies.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/integrate.h"
#include "ferrum/physics/par/integrate_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/tgs_solve.h"

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

#define ASSERT_NEAR(expected, actual, eps)                                     \
    do {                                                                        \
        float _e = (float)(expected);                                          \
        float _a = (float)(actual);                                            \
        if (fabsf(_e - _a) > (eps)) {                                          \
            TEST_FAIL("expected %.8f got %.8f (eps=%.8f)", _e, _a, (float)(eps)); \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Initialize a dynamic body with given position and velocity.
 */
static void make_dynamic_body(phys_body_t *b, float px, float py, float pz,
                               float vx, float vy, float vz) {
    phys_body_init(b);
    phys_body_set_mass(b, 1.0f);
    b->position    = (phys_vec3_t){px, py, pz};
    b->linear_vel  = (phys_vec3_t){vx, vy, vz};
    b->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Compare two body arrays for approximate equality.
 */
static int bodies_match(const phys_body_t *a, const phys_body_t *b,
                         uint32_t count, float eps) {
    for (uint32_t i = 0; i < count; ++i) {
        if (fabsf(a[i].position.x - b[i].position.x) > eps) return 0;
        if (fabsf(a[i].position.y - b[i].position.y) > eps) return 0;
        if (fabsf(a[i].position.z - b[i].position.z) > eps) return 0;
        if (fabsf(a[i].linear_vel.x - b[i].linear_vel.x) > eps) return 0;
        if (fabsf(a[i].linear_vel.y - b[i].linear_vel.y) > eps) return 0;
        if (fabsf(a[i].linear_vel.z - b[i].linear_vel.z) > eps) return 0;
        if (fabsf(a[i].angular_vel.x - b[i].angular_vel.x) > eps) return 0;
        if (fabsf(a[i].angular_vel.y - b[i].angular_vel.y) > eps) return 0;
        if (fabsf(a[i].angular_vel.z - b[i].angular_vel.z) > eps) return 0;
        if (fabsf(a[i].orientation.x - b[i].orientation.x) > eps) return 0;
        if (fabsf(a[i].orientation.y - b[i].orientation.y) > eps) return 0;
        if (fabsf(a[i].orientation.z - b[i].orientation.z) > eps) return 0;
        if (fabsf(a[i].orientation.w - b[i].orientation.w) > eps) return 0;
        if (a[i].flags != b[i].flags) return 0;
        if (a[i].sleep_counter != b[i].sleep_counter) return 0;
    }
    return 1;
}

/* ── Job system setup/teardown ──────────────────────────────────── */

static job_system_t g_sys;
static phys_job_context_t g_ctx;

static void setup_job_system(void) {
    job_system_create(&g_sys, 2, 256, 65536, 64, 0);
    job_system_start(&g_sys);
    phys_job_context_init(&g_ctx, &g_sys);
}

static void teardown_job_system(void) {
    phys_job_context_destroy(&g_ctx);
    job_system_shutdown(&g_sys);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * @brief 100 bodies: parallel output must match sequential exactly.
 */
static int test_par_integrate_identical_to_seq(void) {
    setup_job_system();

    const uint32_t N = 100;
    phys_body_t *bodies_in  = calloc(N, sizeof(phys_body_t));
    phys_velocity_t *vels   = calloc(N, sizeof(phys_velocity_t));
    phys_body_t *out_seq    = calloc(N, sizeof(phys_body_t));
    phys_body_t *out_par    = calloc(N, sizeof(phys_body_t));

    /* Initialize bodies with varied positions and velocities. */
    for (uint32_t i = 0; i < N; ++i) {
        make_dynamic_body(&bodies_in[i],
                          (float)i * 1.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
        vels[i].linear  = (phys_vec3_t){(float)i * 0.1f, 1.0f, 0.0f};
        vels[i].angular = (phys_vec3_t){0.0f, 0.0f, 0.01f * (float)i};
    }

    phys_integrate_args_t args = {
        .bodies_in              = bodies_in,
        .velocities             = vels,
        .bodies_out             = out_seq,
        .body_count             = N,
        .dt                     = 1.0f / 60.0f,
        .gravity                = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear = 0.05f,
        .sleep_threshold_angular = 0.05f,
        .sleep_delay_frames     = 60,
    };

    /* Sequential. */
    phys_stage_integrate(&args);

    /* Parallel. */
    args.bodies_out = out_par;
    phys_stage_integrate_par(&args, &g_ctx);

    ASSERT_TRUE(bodies_match(out_seq, out_par, N, 1e-6f));

    free(bodies_in);
    free(vels);
    free(out_seq);
    free(out_par);
    teardown_job_system();
    return 0;
}

/**
 * @brief 200 bodies fit in a single batch (< 512).
 */
static int test_par_integrate_single_batch(void) {
    setup_job_system();

    const uint32_t N = 200;
    phys_body_t *bodies_in  = calloc(N, sizeof(phys_body_t));
    phys_velocity_t *vels   = calloc(N, sizeof(phys_velocity_t));
    phys_body_t *out_seq    = calloc(N, sizeof(phys_body_t));
    phys_body_t *out_par    = calloc(N, sizeof(phys_body_t));

    for (uint32_t i = 0; i < N; ++i) {
        make_dynamic_body(&bodies_in[i], 0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
        vels[i].linear  = (phys_vec3_t){1.0f, 0.0f, 0.0f};
        vels[i].angular = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    }

    phys_integrate_args_t args = {
        .bodies_in              = bodies_in,
        .velocities             = vels,
        .bodies_out             = out_seq,
        .body_count             = N,
        .dt                     = 1.0f / 60.0f,
        .gravity                = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear = 0.01f,
        .sleep_threshold_angular = 0.01f,
        .sleep_delay_frames     = 60,
    };

    phys_stage_integrate(&args);

    args.bodies_out = out_par;
    phys_stage_integrate_par(&args, &g_ctx);

    /* All 200 bodies in one batch → must match. */
    ASSERT_TRUE(bodies_match(out_seq, out_par, N, 1e-6f));

    free(bodies_in);
    free(vels);
    free(out_seq);
    free(out_par);
    teardown_job_system();
    return 0;
}

/**
 * @brief 2000 bodies → ceil(2000/512) = 4 batches.
 */
static int test_par_integrate_multiple_batches(void) {
    setup_job_system();

    const uint32_t N = 2000;
    phys_body_t *bodies_in  = calloc(N, sizeof(phys_body_t));
    phys_velocity_t *vels   = calloc(N, sizeof(phys_velocity_t));
    phys_body_t *out_seq    = calloc(N, sizeof(phys_body_t));
    phys_body_t *out_par    = calloc(N, sizeof(phys_body_t));

    for (uint32_t i = 0; i < N; ++i) {
        make_dynamic_body(&bodies_in[i],
                          (float)(i % 50), (float)(i / 50), 0.0f,
                          0.0f, 0.0f, 0.0f);
        vels[i].linear  = (phys_vec3_t){0.5f, -0.5f, 0.1f};
        vels[i].angular = (phys_vec3_t){0.0f, 0.1f, 0.0f};
    }

    phys_integrate_args_t args = {
        .bodies_in              = bodies_in,
        .velocities             = vels,
        .bodies_out             = out_seq,
        .body_count             = N,
        .dt                     = 1.0f / 60.0f,
        .gravity                = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear = 0.01f,
        .sleep_threshold_angular = 0.01f,
        .sleep_delay_frames     = 60,
    };

    phys_stage_integrate(&args);

    args.bodies_out = out_par;
    phys_stage_integrate_par(&args, &g_ctx);

    ASSERT_TRUE(bodies_match(out_seq, out_par, N, 1e-6f));

    free(bodies_in);
    free(vels);
    free(out_seq);
    free(out_par);
    teardown_job_system();
    return 0;
}

/**
 * @brief Zero bodies — must not crash.
 */
static int test_par_integrate_zero_bodies(void) {
    setup_job_system();

    phys_body_t dummy_in, dummy_out;
    phys_velocity_t dummy_vel;
    phys_body_init(&dummy_in);
    memset(&dummy_vel, 0, sizeof(dummy_vel));

    phys_integrate_args_t args = {
        .bodies_in              = &dummy_in,
        .velocities             = &dummy_vel,
        .bodies_out             = &dummy_out,
        .body_count             = 0,
        .dt                     = 1.0f / 60.0f,
        .gravity                = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear = 0.05f,
        .sleep_threshold_angular = 0.05f,
        .sleep_delay_frames     = 60,
    };

    /* Must not crash or hang. */
    phys_stage_integrate_par(&args, &g_ctx);

    teardown_job_system();
    return 0;
}

/**
 * @brief Verify that bodies fall under gravity correctly.
 */
static int test_par_integrate_gravity(void) {
    setup_job_system();

    const uint32_t N = 1;
    phys_body_t body_in;
    phys_velocity_t vel;
    phys_body_t out_par;

    make_dynamic_body(&body_in, 0.0f, 10.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    vel.linear  = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    vel.angular = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    float dt = 1.0f / 60.0f;
    phys_integrate_args_t args = {
        .bodies_in              = &body_in,
        .velocities             = &vel,
        .bodies_out             = &out_par,
        .body_count             = N,
        .dt                     = dt,
        .gravity                = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear = 0.01f,
        .sleep_threshold_angular = 0.01f,
        .sleep_delay_frames     = 60,
    };

    phys_stage_integrate_par(&args, &g_ctx);

    /* After one frame: velocity should be gravity * dt downward. */
    float expected_vy = -9.81f * dt;
    ASSERT_NEAR(expected_vy, out_par.linear_vel.y, 1e-5f);

    /* Position: y = 10 + expected_vy * dt */
    float expected_py = 10.0f + expected_vy * dt;
    ASSERT_NEAR(expected_py, out_par.position.y, 1e-5f);

    /* X and Z unchanged. */
    ASSERT_NEAR(0.0f, out_par.position.x, 1e-6f);
    ASSERT_NEAR(0.0f, out_par.position.z, 1e-6f);

    teardown_job_system();
    return 0;
}

/**
 * @brief Body with very low velocity → sleep counter increments.
 */
static int test_par_integrate_sleep(void) {
    setup_job_system();

    const uint32_t N = 1;
    phys_body_t body_in;
    phys_velocity_t vel;
    phys_body_t body_out;

    make_dynamic_body(&body_in, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
    body_in.sleep_counter = 5;

    /* Very low velocity — below default thresholds. */
    vel.linear  = (phys_vec3_t){0.001f, 0.001f, 0.0f};
    vel.angular = (phys_vec3_t){0.0f, 0.0f, 0.0f};

    phys_integrate_args_t args = {
        .bodies_in              = &body_in,
        .velocities             = &vel,
        .bodies_out             = &body_out,
        .body_count             = N,
        .dt                     = 1.0f / 60.0f,
        .gravity                = {0.0f, 0.0f, 0.0f},  /* No gravity to keep vel low. */
        .sleep_threshold_linear = 0.05f,
        .sleep_threshold_angular = 0.05f,
        .sleep_delay_frames     = 60,
    };

    phys_stage_integrate_par(&args, &g_ctx);

    /* sleep_counter should increment from 5 to 6. */
    ASSERT_EQ_UINT(6, body_out.sleep_counter);

    /* Not yet sleeping (needs 60 frames). */
    ASSERT_TRUE(!(body_out.flags & PHYS_BODY_FLAG_SLEEPING));

    teardown_job_system();
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_integrate_identical_to_seq", test_par_integrate_identical_to_seq},
    {"par_integrate_single_batch",     test_par_integrate_single_batch},
    {"par_integrate_multiple_batches",  test_par_integrate_multiple_batches},
    {"par_integrate_zero_bodies",      test_par_integrate_zero_bodies},
    {"par_integrate_gravity",          test_par_integrate_gravity},
    {"par_integrate_sleep",            test_par_integrate_sleep},
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
