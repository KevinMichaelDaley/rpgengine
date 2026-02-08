/**
 * @file p069_physics_par_xpbd_tests.c
 * @brief Tests for parallel XPBD solve (phys-309b).
 *
 * Validates that phys_stage_xpbd_solve_par produces identical results
 * to the sequential version, handles edge cases (zero bodies, single
 * body, no constraints), correct batch count, and determinism.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/par/xpbd_solve_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/xpbd_solve.h"

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

#define ASSERT_FLOAT_EQ(expected, actual)                                      \
    do {                                                                        \
        float _exp = (expected);                                               \
        float _act = (actual);                                                 \
        if (fabsf(_exp - _act) > 1e-4f) {                                     \
            TEST_FAIL("expected %f got %f", (double)_exp, (double)_act);       \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ── Helpers ────────────────────────────────────────────────────── */

/** Identity quaternion. */
static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};

/**
 * @brief Test data structure for XPBD parallel tests.
 */
typedef struct test_data {
    phys_body_t       *bodies_in;
    phys_body_t       *bodies_out_seq;
    phys_body_t       *bodies_out_par;
    phys_velocity_t   *vel_seq;
    phys_velocity_t   *vel_par;
    phys_constraint_t *constraints_seq;
    phys_constraint_t *constraints_par;
    uint32_t           body_count;
    uint32_t           constraint_count;

    job_system_t       job_sys;
    phys_job_context_t job_ctx;
} test_data_t;

/**
 * @brief Build a simple contact constraint between body_a and body_b.
 *
 * Creates a normal-direction constraint along +Y with a small
 * penetration bias to exercise the solver.
 */
static void build_simple_constraint(phys_constraint_t *c,
                                    uint32_t body_a,
                                    uint32_t body_b)
{
    memset(c, 0, sizeof(*c));
    c->body_a = body_a;
    c->body_b = body_b;
    c->row_count = 1;

    /* Normal row along +Y. */
    phys_jacobian_row_t *row = &c->rows[0];
    row->J_va = (phys_vec3_t){0.0f, -1.0f, 0.0f};
    row->J_wa = (phys_vec3_t){0.0f,  0.0f, 0.0f};
    row->J_vb = (phys_vec3_t){0.0f,  1.0f, 0.0f};
    row->J_wb = (phys_vec3_t){0.0f,  0.0f, 0.0f};
    row->effective_mass = 0.5f;
    row->bias = 5.0f;   /* Penetration bias to drive correction. */
    row->lambda = 0.0f;
    row->lambda_min = 0.0f;
    row->lambda_max = 1e10f;
}

/**
 * @brief Set up N dynamic bodies with pairwise constraints.
 *
 * Bodies are at positions (i, 0, 0) with mass 1.0.
 * Constraints link consecutive pairs: (0,1), (1,2), ... (N-2, N-1).
 */
static void setup_test_data(test_data_t *td, uint32_t body_count)
{
    memset(td, 0, sizeof(*td));
    td->body_count = body_count;

    /* Constraint count: one per consecutive pair. */
    td->constraint_count = (body_count > 1) ? body_count - 1 : 0;

    td->bodies_in      = calloc(body_count > 0 ? body_count : 1,
                                sizeof(phys_body_t));
    td->bodies_out_seq = calloc(body_count > 0 ? body_count : 1,
                                sizeof(phys_body_t));
    td->bodies_out_par = calloc(body_count > 0 ? body_count : 1,
                                sizeof(phys_body_t));
    td->vel_seq        = calloc(body_count > 0 ? body_count : 1,
                                sizeof(phys_velocity_t));
    td->vel_par        = calloc(body_count > 0 ? body_count : 1,
                                sizeof(phys_velocity_t));

    uint32_t cc = td->constraint_count > 0 ? td->constraint_count : 1;
    td->constraints_seq = calloc(cc, sizeof(phys_constraint_t));
    td->constraints_par = calloc(cc, sizeof(phys_constraint_t));

    /* Initialize bodies. */
    for (uint32_t i = 0; i < body_count; i++) {
        phys_body_init(&td->bodies_in[i]);
        td->bodies_in[i].position = (phys_vec3_t){(float)i, 0.0f, 0.0f};
        td->bodies_in[i].orientation = QUAT_IDENTITY;
        phys_body_set_mass(&td->bodies_in[i], 1.0f);
    }

    /* Build constraints for consecutive pairs. */
    for (uint32_t i = 0; i < td->constraint_count; i++) {
        build_simple_constraint(&td->constraints_seq[i], i, i + 1);
        build_simple_constraint(&td->constraints_par[i], i, i + 1);
    }

    /* Job system. */
    job_system_create(&td->job_sys, 2, 256, 65536, 64, 0);
    job_system_start(&td->job_sys);
    phys_job_context_init(&td->job_ctx, &td->job_sys);
}

static void teardown_test_data(test_data_t *td)
{
    phys_job_context_destroy(&td->job_ctx);
    job_system_shutdown(&td->job_sys);
    free(td->bodies_in);
    free(td->bodies_out_seq);
    free(td->bodies_out_par);
    free(td->vel_seq);
    free(td->vel_par);
    free(td->constraints_seq);
    free(td->constraints_par);
}

/**
 * @brief Compare body position/velocity arrays for approximate equality.
 * @return 0 if all match within tolerance, 1 on mismatch.
 */
static int compare_results(const phys_body_t *bodies_a,
                           const phys_body_t *bodies_b,
                           const phys_velocity_t *vel_a,
                           const phys_velocity_t *vel_b,
                           uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (fabsf(bodies_a[i].position.x - bodies_b[i].position.x) > 1e-4f ||
            fabsf(bodies_a[i].position.y - bodies_b[i].position.y) > 1e-4f ||
            fabsf(bodies_a[i].position.z - bodies_b[i].position.z) > 1e-4f) {
            fprintf(stderr, "  Body %u position mismatch: "
                    "(%f,%f,%f) vs (%f,%f,%f)\n", i,
                    (double)bodies_a[i].position.x,
                    (double)bodies_a[i].position.y,
                    (double)bodies_a[i].position.z,
                    (double)bodies_b[i].position.x,
                    (double)bodies_b[i].position.y,
                    (double)bodies_b[i].position.z);
            return 1;
        }
        if (vel_a && vel_b) {
            if (fabsf(vel_a[i].linear.x - vel_b[i].linear.x) > 1e-4f ||
                fabsf(vel_a[i].linear.y - vel_b[i].linear.y) > 1e-4f ||
                fabsf(vel_a[i].linear.z - vel_b[i].linear.z) > 1e-4f) {
                fprintf(stderr, "  Body %u velocity mismatch\n", i);
                return 1;
            }
        }
    }
    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Parallel XPBD produces identical results to sequential.
 */
static int test_par_xpbd_identical_to_seq(void)
{
    test_data_t td;
    setup_test_data(&td, 10);

    phys_xpbd_solve_args_t seq_args = {
        .constraints      = td.constraints_seq,
        .constraint_count = td.constraint_count,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_seq,
        .velocities_out   = td.vel_seq,
        .body_count       = td.body_count,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };
    phys_stage_xpbd_solve(&seq_args);

    phys_xpbd_solve_args_t par_args = {
        .constraints      = td.constraints_par,
        .constraint_count = td.constraint_count,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_par,
        .velocities_out   = td.vel_par,
        .body_count       = td.body_count,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_xpbd_solve_par(&par_args, &td.job_ctx, &arena);

    phys_frame_arena_destroy(&arena);

    int cmp = compare_results(td.bodies_out_seq, td.bodies_out_par,
                              td.vel_seq, td.vel_par, td.body_count);
    ASSERT_TRUE(cmp == 0);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 2: 500 bodies → ceil(499 / 128) = 4 constraint batches.
 */
static int test_par_xpbd_batch_128(void)
{
    test_data_t td;
    setup_test_data(&td, 500);

    /* 499 constraints → ceil(499/128) = 4 batches. */
    uint32_t expected_batches =
        (td.constraint_count + PHYS_XPBD_SOLVE_BATCH_SIZE - 1)
        / PHYS_XPBD_SOLVE_BATCH_SIZE;
    ASSERT_EQ_UINT(4, expected_batches);

    phys_xpbd_solve_args_t par_args = {
        .constraints      = td.constraints_par,
        .constraint_count = td.constraint_count,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_par,
        .velocities_out   = td.vel_par,
        .body_count       = td.body_count,
        .iterations       = 2,
        .omega            = 0.6f,
        .dt               = 1.0f / 60.0f,
    };
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_xpbd_solve_par(&par_args, &td.job_ctx, &arena);

    phys_frame_arena_destroy(&arena);

    /* Verify that some position correction occurred. */
    int any_moved = 0;
    for (uint32_t i = 0; i < td.body_count; i++) {
        if (fabsf(td.bodies_out_par[i].position.y -
                  td.bodies_in[i].position.y) > 1e-6f) {
            any_moved = 1;
            break;
        }
    }
    ASSERT_TRUE(any_moved);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 3: Zero bodies — no crash.
 */
static int test_par_xpbd_zero_bodies(void)
{
    test_data_t td;
    setup_test_data(&td, 0);

    phys_xpbd_solve_args_t par_args = {
        .constraints      = td.constraints_par,
        .constraint_count = 0,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_par,
        .velocities_out   = td.vel_par,
        .body_count       = 0,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    /* Should not crash. */
    phys_stage_xpbd_solve_par(&par_args, &td.job_ctx, &arena);

    /* Also test NULL args. */
    phys_stage_xpbd_solve_par(NULL, &td.job_ctx, &arena);

    phys_frame_arena_destroy(&arena);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 4: Single body with one self-referencing constraint → 1 batch.
 */
static int test_par_xpbd_single_body(void)
{
    test_data_t td;
    memset(&td, 0, sizeof(td));
    td.body_count = 1;
    td.constraint_count = 1;

    td.bodies_in      = calloc(1, sizeof(phys_body_t));
    td.bodies_out_seq = calloc(1, sizeof(phys_body_t));
    td.bodies_out_par = calloc(1, sizeof(phys_body_t));
    td.vel_seq        = calloc(1, sizeof(phys_velocity_t));
    td.vel_par        = calloc(1, sizeof(phys_velocity_t));
    td.constraints_seq = calloc(1, sizeof(phys_constraint_t));
    td.constraints_par = calloc(1, sizeof(phys_constraint_t));

    phys_body_init(&td.bodies_in[0]);
    td.bodies_in[0].position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    td.bodies_in[0].orientation = QUAT_IDENTITY;
    phys_body_set_mass(&td.bodies_in[0], 1.0f);

    /* Self-constraint (body 0 ↔ body 0). */
    build_simple_constraint(&td.constraints_seq[0], 0, 0);
    build_simple_constraint(&td.constraints_par[0], 0, 0);

    job_system_create(&td.job_sys, 1, 256, 65536, 64, 0);
    job_system_start(&td.job_sys);
    phys_job_context_init(&td.job_ctx, &td.job_sys);

    /* Sequential. */
    phys_xpbd_solve_args_t seq_args = {
        .constraints      = td.constraints_seq,
        .constraint_count = 1,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_seq,
        .velocities_out   = td.vel_seq,
        .body_count       = 1,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };
    phys_stage_xpbd_solve(&seq_args);

    /* Parallel. */
    phys_xpbd_solve_args_t par_args = {
        .constraints      = td.constraints_par,
        .constraint_count = 1,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_par,
        .velocities_out   = td.vel_par,
        .body_count       = 1,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_xpbd_solve_par(&par_args, &td.job_ctx, &arena);

    phys_frame_arena_destroy(&arena);

    int cmp = compare_results(td.bodies_out_seq, td.bodies_out_par,
                              td.vel_seq, td.vel_par, 1);
    ASSERT_TRUE(cmp == 0);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 5: Bodies with no constraints — positions unchanged.
 */
static int test_par_xpbd_no_constraints(void)
{
    test_data_t td;
    memset(&td, 0, sizeof(td));
    td.body_count = 5;
    td.constraint_count = 0;

    td.bodies_in       = calloc(5, sizeof(phys_body_t));
    td.bodies_out_seq  = calloc(5, sizeof(phys_body_t));
    td.bodies_out_par  = calloc(5, sizeof(phys_body_t));
    td.vel_seq         = calloc(5, sizeof(phys_velocity_t));
    td.vel_par         = calloc(5, sizeof(phys_velocity_t));
    td.constraints_seq = calloc(1, sizeof(phys_constraint_t));
    td.constraints_par = calloc(1, sizeof(phys_constraint_t));

    for (uint32_t i = 0; i < 5; i++) {
        phys_body_init(&td.bodies_in[i]);
        td.bodies_in[i].position = (phys_vec3_t){(float)i, 1.0f, 2.0f};
        td.bodies_in[i].orientation = QUAT_IDENTITY;
        phys_body_set_mass(&td.bodies_in[i], 1.0f);
    }

    job_system_create(&td.job_sys, 1, 256, 65536, 64, 0);
    job_system_start(&td.job_sys);
    phys_job_context_init(&td.job_ctx, &td.job_sys);

    phys_xpbd_solve_args_t par_args = {
        .constraints      = td.constraints_par,
        .constraint_count = 0,
        .bodies_in        = td.bodies_in,
        .bodies_out       = td.bodies_out_par,
        .velocities_out   = td.vel_par,
        .body_count       = 5,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    phys_stage_xpbd_solve_par(&par_args, &td.job_ctx, &arena);

    phys_frame_arena_destroy(&arena);

    /* Positions should be copied through unchanged. */
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_FLOAT_EQ(td.bodies_in[i].position.x,
                        td.bodies_out_par[i].position.x);
        ASSERT_FLOAT_EQ(td.bodies_in[i].position.y,
                        td.bodies_out_par[i].position.y);
        ASSERT_FLOAT_EQ(td.bodies_in[i].position.z,
                        td.bodies_out_par[i].position.z);
    }

    /* Velocities should be zero (no position change). */
    for (uint32_t i = 0; i < 5; i++) {
        ASSERT_FLOAT_EQ(0.0f, td.vel_par[i].linear.x);
        ASSERT_FLOAT_EQ(0.0f, td.vel_par[i].linear.y);
        ASSERT_FLOAT_EQ(0.0f, td.vel_par[i].linear.z);
    }

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 6: Deterministic — same input always produces the same output.
 */
static int test_par_xpbd_deterministic(void)
{
    /* Run the parallel solver twice with identical inputs. */
    phys_body_t bodies_in[10];
    phys_body_t bodies_out_a[10];
    phys_body_t bodies_out_b[10];
    phys_velocity_t vel_a[10];
    phys_velocity_t vel_b[10];
    phys_constraint_t constraints_a[9];
    phys_constraint_t constraints_b[9];

    for (uint32_t i = 0; i < 10; i++) {
        phys_body_init(&bodies_in[i]);
        bodies_in[i].position = (phys_vec3_t){(float)i, 0.0f, 0.0f};
        bodies_in[i].orientation = QUAT_IDENTITY;
        phys_body_set_mass(&bodies_in[i], 1.0f);
    }
    for (uint32_t i = 0; i < 9; i++) {
        build_simple_constraint(&constraints_a[i], i, i + 1);
        build_simple_constraint(&constraints_b[i], i, i + 1);
    }

    job_system_t sys;
    job_system_create(&sys, 2, 256, 65536, 64, 0);
    job_system_start(&sys);
    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);

    /* Run A. */
    phys_xpbd_solve_args_t args_a = {
        .constraints      = constraints_a,
        .constraint_count = 9,
        .bodies_in        = bodies_in,
        .bodies_out       = bodies_out_a,
        .velocities_out   = vel_a,
        .body_count       = 10,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };
    phys_stage_xpbd_solve_par(&args_a, &ctx, &arena);

    /* Run B. */
    phys_xpbd_solve_args_t args_b = {
        .constraints      = constraints_b,
        .constraint_count = 9,
        .bodies_in        = bodies_in,
        .bodies_out       = bodies_out_b,
        .velocities_out   = vel_b,
        .body_count       = 10,
        .iterations       = 4,
        .omega            = 0.7f,
        .dt               = 1.0f / 60.0f,
    };
    phys_stage_xpbd_solve_par(&args_b, &ctx, &arena);

    phys_frame_arena_destroy(&arena);

    int cmp = compare_results(bodies_out_a, bodies_out_b,
                              vel_a, vel_b, 10);
    ASSERT_TRUE(cmp == 0);

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
    {"par_xpbd_identical_to_seq",  test_par_xpbd_identical_to_seq},
    {"par_xpbd_batch_128",         test_par_xpbd_batch_128},
    {"par_xpbd_zero_bodies",       test_par_xpbd_zero_bodies},
    {"par_xpbd_single_body",       test_par_xpbd_single_body},
    {"par_xpbd_no_constraints",    test_par_xpbd_no_constraints},
    {"par_xpbd_deterministic",     test_par_xpbd_deterministic},
};

int main(void)
{
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
