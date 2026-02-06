/**
 * @file p068_physics_par_tgs_tests.c
 * @brief Tests for parallel TGS velocity solver (phys-309).
 *
 * Validates that the parallel solver produces identical results to
 * the sequential solver across a variety of island configurations.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/par/tgs_solve_par.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"

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
        float _e = (expected);                                                  \
        float _a = (actual);                                                    \
        if (fabsf(_e - _a) > (eps)) {                                          \
            TEST_FAIL("expected %.8f got %.8f (eps=%.8f)", _e, _a, (eps));     \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const float EPSILON = 1e-6f;

/* ── Helpers ────────────────────────────────────────────────────── */

/**
 * @brief Create a dynamic body with unit mass and unit diagonal inertia.
 */
static void make_dynamic_body(phys_body_t *body, float vx, float vy, float vz)
{
    phys_body_init(body);
    phys_body_set_mass(body, 1.0f);
    body->inv_inertia_diag = (phys_vec3_t){1.0f, 1.0f, 1.0f};
    body->linear_vel  = (phys_vec3_t){vx, vy, vz};
    body->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Build a simple normal-only constraint between two bodies.
 *
 * Creates a constraint with 1 row (normal direction along +Y),
 * with a small bias to drive convergence, and large lambda bounds.
 */
static void make_simple_constraint(phys_constraint_t *c,
                                    uint32_t body_a,
                                    uint32_t body_b)
{
    memset(c, 0, sizeof(*c));
    c->body_a = body_a;
    c->body_b = body_b;
    c->row_count = 1;

    phys_jacobian_row_t *row = &c->rows[0];
    /* Normal along +Y: body A pushed up, body B pushed down. */
    row->J_va = (phys_vec3_t){0.0f,  1.0f, 0.0f};
    row->J_wa = (phys_vec3_t){0.0f,  0.0f, 0.0f};
    row->J_vb = (phys_vec3_t){0.0f, -1.0f, 0.0f};
    row->J_wb = (phys_vec3_t){0.0f,  0.0f, 0.0f};
    row->effective_mass = 0.5f;  /* 1/(inv_mass_a + inv_mass_b) = 1/2 */
    row->bias = 0.1f;
    row->lambda = 0.0f;
    row->lambda_min = 0.0f;
    row->lambda_max = 1000.0f;
}

/**
 * @brief Compare two velocity arrays element-by-element.
 * @return 0 if equal within epsilon, 1 otherwise.
 */
static int velocities_equal(const phys_velocity_t *a,
                             const phys_velocity_t *b,
                             uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        if (fabsf(a[i].linear.x  - b[i].linear.x)  > EPSILON) return 1;
        if (fabsf(a[i].linear.y  - b[i].linear.y)  > EPSILON) return 1;
        if (fabsf(a[i].linear.z  - b[i].linear.z)  > EPSILON) return 1;
        if (fabsf(a[i].angular.x - b[i].angular.x) > EPSILON) return 1;
        if (fabsf(a[i].angular.y - b[i].angular.y) > EPSILON) return 1;
        if (fabsf(a[i].angular.z - b[i].angular.z) > EPSILON) return 1;
    }
    return 0;
}

/**
 * @brief Reset all constraint lambdas to zero for a fresh solver run.
 */
static void reset_constraint_lambdas(phys_constraint_t *constraints,
                                      uint32_t count)
{
    for (uint32_t i = 0; i < count; i++) {
        for (uint8_t r = 0; r < constraints[i].row_count; r++) {
            constraints[i].rows[r].lambda = 0.0f;
        }
    }
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Two islands with 4 bodies each. Parallel output must match
 *         sequential output exactly.
 */
static int test_par_tgs_identical_to_seq(void)
{
    /* 8 bodies: island 0 = bodies {0,1,2,3}, island 1 = bodies {4,5,6,7}. */
    const uint32_t body_count = 8;
    const uint32_t iterations = 10;

    phys_body_t bodies[8];
    for (uint32_t i = 0; i < body_count; i++) {
        make_dynamic_body(&bodies[i], (float)i * 0.1f, 0.0f, 0.0f);
    }

    /* 6 constraints: 3 per island. */
    phys_constraint_t constraints[6];
    /* Island 0 constraints. */
    make_simple_constraint(&constraints[0], 0, 1);
    make_simple_constraint(&constraints[1], 1, 2);
    make_simple_constraint(&constraints[2], 2, 3);
    /* Island 1 constraints. */
    make_simple_constraint(&constraints[3], 4, 5);
    make_simple_constraint(&constraints[4], 5, 6);
    make_simple_constraint(&constraints[5], 6, 7);

    /* Build islands manually. */
    uint32_t island0_bodies[] = {0, 1, 2, 3};
    uint32_t island0_constraints[] = {0, 1, 2};
    uint32_t island1_bodies[] = {4, 5, 6, 7};
    uint32_t island1_constraints[] = {3, 4, 5};

    phys_island_t islands[2];
    islands[0] = (phys_island_t){
        .body_indices = island0_bodies, .body_count = 4,
        .constraint_indices = island0_constraints, .constraint_count = 3,
        .sleeping = false,
    };
    islands[1] = (phys_island_t){
        .body_indices = island1_bodies, .body_count = 4,
        .constraint_indices = island1_constraints, .constraint_count = 3,
        .sleeping = false,
    };

    phys_island_list_t island_list = {
        .islands = islands, .count = 2, .capacity = 2,
    };

    /* Run sequential solver. */
    phys_velocity_t vel_seq[8];
    phys_tgs_solve_args_t args_seq = {
        .islands     = &island_list,
        .constraints = constraints,
        .bodies      = bodies,
        .velocities  = vel_seq,
        .body_count  = body_count,
        .iterations  = iterations,
    };
    phys_stage_tgs_solve(&args_seq);

    /* Save sequential results. */
    phys_velocity_t expected[8];
    memcpy(expected, vel_seq, sizeof(expected));

    /* Reset lambdas for parallel run. */
    reset_constraint_lambdas(constraints, 6);

    /* Run parallel solver. */
    job_system_t sys;
    job_system_create(&sys, 2, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_velocity_t vel_par[8];
    phys_tgs_solve_args_t args_par = {
        .islands     = &island_list,
        .constraints = constraints,
        .bodies      = bodies,
        .velocities  = vel_par,
        .body_count  = body_count,
        .iterations  = iterations,
    };
    phys_stage_tgs_solve_par(&args_par, &ctx);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);

    /* Compare results. */
    ASSERT_TRUE(velocities_equal(expected, vel_par, body_count) == 0);

    return 0;
}

/**
 * Test 2: Single island → 1 job dispatched.
 */
static int test_par_tgs_single_island(void)
{
    const uint32_t body_count = 4;
    const uint32_t iterations = 8;

    phys_body_t bodies[4];
    for (uint32_t i = 0; i < body_count; i++) {
        make_dynamic_body(&bodies[i], 1.0f, -0.5f, 0.0f);
    }

    phys_constraint_t constraints[2];
    make_simple_constraint(&constraints[0], 0, 1);
    make_simple_constraint(&constraints[1], 2, 3);

    uint32_t island_bodies[] = {0, 1, 2, 3};
    uint32_t island_constraints[] = {0, 1};

    phys_island_t island = {
        .body_indices = island_bodies, .body_count = 4,
        .constraint_indices = island_constraints, .constraint_count = 2,
        .sleeping = false,
    };
    phys_island_list_t island_list = {
        .islands = &island, .count = 1, .capacity = 1,
    };

    /* Sequential reference. */
    phys_velocity_t vel_seq[4];
    phys_tgs_solve_args_t args = {
        .islands = &island_list, .constraints = constraints,
        .bodies = bodies, .velocities = vel_seq,
        .body_count = body_count, .iterations = iterations,
    };
    phys_stage_tgs_solve(&args);
    phys_velocity_t expected[4];
    memcpy(expected, vel_seq, sizeof(expected));

    /* Reset and run parallel. */
    reset_constraint_lambdas(constraints, 2);

    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_velocity_t vel_par[4];
    args.velocities = vel_par;
    phys_stage_tgs_solve_par(&args, &ctx);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);

    ASSERT_TRUE(velocities_equal(expected, vel_par, body_count) == 0);

    return 0;
}

/**
 * Test 3: 10 islands → 10 jobs dispatched.
 */
static int test_par_tgs_many_islands(void)
{
    /* 20 bodies: 2 bodies per island, 10 islands. */
    const uint32_t body_count = 20;
    const uint32_t island_count = 10;
    const uint32_t iterations = 6;

    phys_body_t bodies[20];
    for (uint32_t i = 0; i < body_count; i++) {
        make_dynamic_body(&bodies[i], (float)i * 0.05f, 0.0f, 0.0f);
    }

    /* 1 constraint per island. */
    phys_constraint_t constraints[10];
    for (uint32_t i = 0; i < island_count; i++) {
        make_simple_constraint(&constraints[i], i * 2, i * 2 + 1);
    }

    /* Build islands. */
    uint32_t body_idx_storage[20];
    uint32_t cons_idx_storage[10];
    phys_island_t islands[10];
    for (uint32_t i = 0; i < island_count; i++) {
        body_idx_storage[i * 2]     = i * 2;
        body_idx_storage[i * 2 + 1] = i * 2 + 1;
        cons_idx_storage[i] = i;
        islands[i] = (phys_island_t){
            .body_indices = &body_idx_storage[i * 2], .body_count = 2,
            .constraint_indices = &cons_idx_storage[i], .constraint_count = 1,
            .sleeping = false,
        };
    }

    phys_island_list_t island_list = {
        .islands = islands, .count = island_count, .capacity = island_count,
    };

    /* Sequential reference. */
    phys_velocity_t vel_seq[20];
    phys_tgs_solve_args_t args = {
        .islands = &island_list, .constraints = constraints,
        .bodies = bodies, .velocities = vel_seq,
        .body_count = body_count, .iterations = iterations,
    };
    phys_stage_tgs_solve(&args);
    phys_velocity_t expected[20];
    memcpy(expected, vel_seq, sizeof(expected));

    /* Reset and run parallel. */
    reset_constraint_lambdas(constraints, island_count);

    job_system_t sys;
    job_system_create(&sys, 2, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_velocity_t vel_par[20];
    args.velocities = vel_par;
    phys_stage_tgs_solve_par(&args, &ctx);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);

    ASSERT_TRUE(velocities_equal(expected, vel_par, body_count) == 0);

    return 0;
}

/**
 * Test 4: Zero islands — should not crash or hang.
 */
static int test_par_tgs_zero_islands(void)
{
    phys_island_list_t island_list = {
        .islands = NULL, .count = 0, .capacity = 0,
    };

    phys_body_t bodies[2];
    make_dynamic_body(&bodies[0], 1.0f, 0.0f, 0.0f);
    make_dynamic_body(&bodies[1], 0.0f, 1.0f, 0.0f);

    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));

    phys_tgs_solve_args_t args = {
        .islands = &island_list, .constraints = NULL,
        .bodies = bodies, .velocities = velocities,
        .body_count = 2, .iterations = 10,
    };

    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    /* Should return without crashing. */
    phys_stage_tgs_solve_par(&args, &ctx);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);

    return 0;
}

/**
 * Test 5: Island with bodies but no constraints — velocities should
 *         match initial body velocities (just copied, not modified).
 */
static int test_par_tgs_no_constraints(void)
{
    const uint32_t body_count = 4;

    phys_body_t bodies[4];
    make_dynamic_body(&bodies[0], 1.0f, 2.0f, 3.0f);
    make_dynamic_body(&bodies[1], 4.0f, 5.0f, 6.0f);
    make_dynamic_body(&bodies[2], 7.0f, 8.0f, 9.0f);
    make_dynamic_body(&bodies[3], 0.1f, 0.2f, 0.3f);

    uint32_t island_bodies[] = {0, 1, 2, 3};
    phys_island_t island = {
        .body_indices = island_bodies, .body_count = 4,
        .constraint_indices = NULL, .constraint_count = 0,
        .sleeping = false,
    };
    phys_island_list_t island_list = {
        .islands = &island, .count = 1, .capacity = 1,
    };

    job_system_t sys;
    job_system_create(&sys, 1, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    phys_velocity_t velocities[4];
    phys_tgs_solve_args_t args = {
        .islands = &island_list, .constraints = NULL,
        .bodies = bodies, .velocities = velocities,
        .body_count = body_count, .iterations = 10,
    };
    phys_stage_tgs_solve_par(&args, &ctx);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);

    /* Velocities should equal the initial body velocities (just copied). */
    for (uint32_t i = 0; i < body_count; i++) {
        ASSERT_FLOAT_EQ(bodies[i].linear_vel.x,  velocities[i].linear.x,  EPSILON);
        ASSERT_FLOAT_EQ(bodies[i].linear_vel.y,  velocities[i].linear.y,  EPSILON);
        ASSERT_FLOAT_EQ(bodies[i].linear_vel.z,  velocities[i].linear.z,  EPSILON);
        ASSERT_FLOAT_EQ(bodies[i].angular_vel.x, velocities[i].angular.x, EPSILON);
        ASSERT_FLOAT_EQ(bodies[i].angular_vel.y, velocities[i].angular.y, EPSILON);
        ASSERT_FLOAT_EQ(bodies[i].angular_vel.z, velocities[i].angular.z, EPSILON);
    }

    return 0;
}

/**
 * Test 6: Deterministic — same input twice must produce identical output.
 */
static int test_par_tgs_deterministic(void)
{
    const uint32_t body_count = 6;
    const uint32_t iterations = 12;

    phys_body_t bodies[6];
    for (uint32_t i = 0; i < body_count; i++) {
        make_dynamic_body(&bodies[i], (float)i * 0.2f, -0.1f * (float)i, 0.0f);
    }

    phys_constraint_t constraints[4];
    make_simple_constraint(&constraints[0], 0, 1);
    make_simple_constraint(&constraints[1], 1, 2);
    make_simple_constraint(&constraints[2], 3, 4);
    make_simple_constraint(&constraints[3], 4, 5);

    uint32_t island0_bodies[] = {0, 1, 2};
    uint32_t island0_constraints[] = {0, 1};
    uint32_t island1_bodies[] = {3, 4, 5};
    uint32_t island1_constraints[] = {2, 3};

    phys_island_t islands[2] = {
        {
            .body_indices = island0_bodies, .body_count = 3,
            .constraint_indices = island0_constraints, .constraint_count = 2,
            .sleeping = false,
        },
        {
            .body_indices = island1_bodies, .body_count = 3,
            .constraint_indices = island1_constraints, .constraint_count = 2,
            .sleeping = false,
        },
    };
    phys_island_list_t island_list = {
        .islands = islands, .count = 2, .capacity = 2,
    };

    job_system_t sys;
    job_system_create(&sys, 2, 256, 65536, 64, 0);
    job_system_start(&sys);

    phys_job_context_t ctx;
    phys_job_context_init(&ctx, &sys);

    /* Run 1. */
    phys_velocity_t vel_run1[6];
    phys_tgs_solve_args_t args = {
        .islands = &island_list, .constraints = constraints,
        .bodies = bodies, .velocities = vel_run1,
        .body_count = body_count, .iterations = iterations,
    };
    phys_stage_tgs_solve_par(&args, &ctx);

    phys_velocity_t saved[6];
    memcpy(saved, vel_run1, sizeof(saved));

    /* Reset lambdas. */
    reset_constraint_lambdas(constraints, 4);

    /* Run 2. */
    phys_velocity_t vel_run2[6];
    args.velocities = vel_run2;
    phys_stage_tgs_solve_par(&args, &ctx);

    phys_job_context_destroy(&ctx);
    job_system_shutdown(&sys);

    /* Both runs must produce identical output. */
    ASSERT_TRUE(velocities_equal(saved, vel_run2, body_count) == 0);

    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_tgs_identical_to_seq", test_par_tgs_identical_to_seq},
    {"par_tgs_single_island",    test_par_tgs_single_island},
    {"par_tgs_many_islands",     test_par_tgs_many_islands},
    {"par_tgs_zero_islands",     test_par_tgs_zero_islands},
    {"par_tgs_no_constraints",   test_par_tgs_no_constraints},
    {"par_tgs_deterministic",    test_par_tgs_deterministic},
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
