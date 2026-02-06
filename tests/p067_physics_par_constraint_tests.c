/**
 * @file p067_physics_par_constraint_tests.c
 * @brief Tests for parallel constraint build (phys-308).
 *
 * Validates that phys_stage_constraint_build_par produces identical
 * results to the sequential phys_stage_constraint_build.
 */

#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_stage.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/par/constraint_build_par.h"

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
 * @brief Initialize a simple test body with unit mass and identity pose.
 */
static void init_test_body(phys_body_t *body) {
    phys_body_init(body);
    phys_body_set_mass(body, 1.0f);
    phys_body_set_sphere_inertia(body, 1.0f, 1.0f);
    body->position   = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    body->linear_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    body->angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
}

/**
 * @brief Initialize a manifold with a given number of contact points.
 *
 * All contacts have a simple downward normal and small penetration.
 */
static void init_test_manifold(phys_manifold_t *m, uint32_t body_a,
                                uint32_t body_b, uint8_t num_points) {
    phys_manifold_init(m, body_a, body_b);
    m->friction    = 0.5f;
    m->restitution = 0.3f;

    if (num_points > PHYS_MAX_MANIFOLD_POINTS) {
        num_points = PHYS_MAX_MANIFOLD_POINTS;
    }
    m->point_count = num_points;

    for (uint8_t p = 0; p < num_points; ++p) {
        m->points[p].normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
        m->points[p].penetration = 0.01f;
        m->points[p].point_world = (phys_vec3_t){(float)p * 0.1f, 0.0f, 0.0f};
        m->points[p].local_a     = m->points[p].point_world;
        m->points[p].local_b     = m->points[p].point_world;
        m->points[p].feature_id  = p;

        m->normal_impulse[p]     = 0.0f;
        m->tangent_impulse[p][0] = 0.0f;
        m->tangent_impulse[p][1] = 0.0f;
    }
}

/**
 * @brief Initialize a neutral stabilization hint (pass-through).
 */
static void init_neutral_hint(phys_stab_hint_t *hint) {
    hint->friction_scale    = 1.0f;
    hint->restitution_scale = 1.0f;
}

/* ── Test infrastructure setup/teardown ─────────────────────────── */

typedef struct test_env {
    job_system_t       sys;
    phys_job_context_t ctx;
} test_env_t;

static void env_setup(test_env_t *env) {
    job_system_create(&env->sys, 2, 256, 65536, 64, 0);
    job_system_start(&env->sys);
    phys_job_context_init(&env->ctx, &env->sys);
}

static void env_teardown(test_env_t *env) {
    phys_job_context_destroy(&env->ctx);
    job_system_shutdown(&env->sys);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * @brief 20 manifolds: parallel produces the same constraint count
 *        as sequential.
 */
static int test_par_cb_identical_to_seq(void) {
    test_env_t env;
    env_setup(&env);

    /* 4 bodies, 20 manifolds each with 2 contact points. */
    enum { NUM_BODIES = 4, NUM_MANIFOLDS = 20, POINTS_PER = 2 };
    enum { MAX_C = NUM_MANIFOLDS * POINTS_PER };

    phys_body_t bodies[NUM_BODIES];
    for (int i = 0; i < NUM_BODIES; ++i) { init_test_body(&bodies[i]); }

    phys_manifold_t manifolds[NUM_MANIFOLDS];
    phys_stab_hint_t hints[NUM_MANIFOLDS];
    for (int i = 0; i < NUM_MANIFOLDS; ++i) {
        init_test_manifold(&manifolds[i], 0, 1, POINTS_PER);
        init_neutral_hint(&hints[i]);
    }

    /* Sequential run. */
    phys_constraint_t seq_out[MAX_C];
    uint32_t seq_count = 0;
    memset(seq_out, 0, sizeof(seq_out));

    phys_constraint_build_args_t args_seq = {
        .manifolds          = manifolds,
        .hints              = hints,
        .manifold_count     = NUM_MANIFOLDS,
        .bodies             = bodies,
        .constraints_out    = seq_out,
        .constraint_count_out = &seq_count,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build(&args_seq);

    /* Parallel run. */
    phys_constraint_t par_out[MAX_C];
    uint32_t par_count = 0;
    memset(par_out, 0, sizeof(par_out));

    phys_constraint_build_args_t args_par = {
        .manifolds          = manifolds,
        .hints              = hints,
        .manifold_count     = NUM_MANIFOLDS,
        .bodies             = bodies,
        .constraints_out    = par_out,
        .constraint_count_out = &par_count,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args_par, &env.ctx);

    /* Same constraint count. */
    ASSERT_EQ_UINT(seq_count, par_count);
    ASSERT_EQ_UINT(NUM_MANIFOLDS * POINTS_PER, par_count);

    env_teardown(&env);
    return 0;
}

/**
 * @brief 100 manifolds → ceil(100/32) = 4 jobs dispatched.
 *        Verify correct total constraint count.
 */
static int test_par_cb_batch_32(void) {
    test_env_t env;
    env_setup(&env);

    enum { NUM_BODIES = 4, NUM_MANIFOLDS = 100, POINTS_PER = 1 };
    enum { MAX_C = NUM_MANIFOLDS * POINTS_PER };

    phys_body_t bodies[NUM_BODIES];
    for (int i = 0; i < NUM_BODIES; ++i) { init_test_body(&bodies[i]); }

    phys_manifold_t *manifolds = calloc(NUM_MANIFOLDS, sizeof(phys_manifold_t));
    phys_stab_hint_t *hints    = calloc(NUM_MANIFOLDS, sizeof(phys_stab_hint_t));
    ASSERT_TRUE(manifolds != NULL);
    ASSERT_TRUE(hints != NULL);

    for (int i = 0; i < NUM_MANIFOLDS; ++i) {
        init_test_manifold(&manifolds[i], 0, 1, POINTS_PER);
        init_neutral_hint(&hints[i]);
    }

    phys_constraint_t *par_out = calloc(MAX_C, sizeof(phys_constraint_t));
    ASSERT_TRUE(par_out != NULL);
    uint32_t par_count = 0;

    phys_constraint_build_args_t args = {
        .manifolds          = manifolds,
        .hints              = hints,
        .manifold_count     = NUM_MANIFOLDS,
        .bodies             = bodies,
        .constraints_out    = par_out,
        .constraint_count_out = &par_count,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args, &env.ctx);

    /* 100 manifolds × 1 point each = 100 constraints. */
    ASSERT_EQ_UINT(100, par_count);

    free(manifolds);
    free(hints);
    free(par_out);
    env_teardown(&env);
    return 0;
}

/**
 * @brief Zero manifolds: no crash, 0 constraints written.
 */
static int test_par_cb_zero_manifolds(void) {
    test_env_t env;
    env_setup(&env);

    phys_body_t bodies[2];
    init_test_body(&bodies[0]);
    init_test_body(&bodies[1]);

    phys_constraint_t par_out[1];
    uint32_t par_count = 99; /* Sentinel to verify it gets set to 0. */

    phys_constraint_build_args_t args = {
        .manifolds          = NULL,
        .hints              = NULL,
        .manifold_count     = 0,
        .bodies             = bodies,
        .constraints_out    = par_out,
        .constraint_count_out = &par_count,
        .max_constraints    = 1,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args, &env.ctx);

    ASSERT_EQ_UINT(0, par_count);

    env_teardown(&env);
    return 0;
}

/**
 * @brief Single manifold with 3 contact points → 3 constraints.
 */
static int test_par_cb_single_manifold(void) {
    test_env_t env;
    env_setup(&env);

    enum { NUM_BODIES = 2, POINTS_PER = 3 };
    enum { MAX_C = POINTS_PER };

    phys_body_t bodies[NUM_BODIES];
    for (int i = 0; i < NUM_BODIES; ++i) { init_test_body(&bodies[i]); }

    phys_manifold_t manifold;
    phys_stab_hint_t hint;
    init_test_manifold(&manifold, 0, 1, POINTS_PER);
    init_neutral_hint(&hint);

    phys_constraint_t par_out[MAX_C];
    uint32_t par_count = 0;
    memset(par_out, 0, sizeof(par_out));

    phys_constraint_build_args_t args = {
        .manifolds          = &manifold,
        .hints              = &hint,
        .manifold_count     = 1,
        .bodies             = bodies,
        .constraints_out    = par_out,
        .constraint_count_out = &par_count,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args, &env.ctx);

    ASSERT_EQ_UINT(POINTS_PER, par_count);

    /* Verify back-references are correctly set. */
    for (uint32_t i = 0; i < par_count; ++i) {
        ASSERT_EQ_UINT(0, par_out[i].body_a);
        ASSERT_EQ_UINT(1, par_out[i].body_b);
        ASSERT_EQ_UINT(0, par_out[i].manifold_idx);
        ASSERT_EQ_UINT(i, par_out[i].point_idx);
    }

    env_teardown(&env);
    return 0;
}

/**
 * @brief Output limit respected: 10 manifolds × 4 points = 40 potential,
 *        but max_constraints = 5. Verify count <= 5.
 */
static int test_par_cb_max_constraints(void) {
    test_env_t env;
    env_setup(&env);

    enum { NUM_BODIES = 2, NUM_MANIFOLDS = 10, POINTS_PER = 4 };
    enum { MAX_C = 5 };

    phys_body_t bodies[NUM_BODIES];
    for (int i = 0; i < NUM_BODIES; ++i) { init_test_body(&bodies[i]); }

    phys_manifold_t manifolds[NUM_MANIFOLDS];
    phys_stab_hint_t hints[NUM_MANIFOLDS];
    for (int i = 0; i < NUM_MANIFOLDS; ++i) {
        init_test_manifold(&manifolds[i], 0, 1, POINTS_PER);
        init_neutral_hint(&hints[i]);
    }

    phys_constraint_t par_out[MAX_C];
    uint32_t par_count = 0;
    memset(par_out, 0, sizeof(par_out));

    phys_constraint_build_args_t args = {
        .manifolds          = manifolds,
        .hints              = hints,
        .manifold_count     = NUM_MANIFOLDS,
        .bodies             = bodies,
        .constraints_out    = par_out,
        .constraint_count_out = &par_count,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args, &env.ctx);

    /* Must not exceed buffer capacity. */
    ASSERT_TRUE(par_count <= MAX_C);

    env_teardown(&env);
    return 0;
}

/**
 * @brief Deterministic: running twice with identical input produces
 *        the same constraint count.
 */
static int test_par_cb_deterministic(void) {
    test_env_t env;
    env_setup(&env);

    enum { NUM_BODIES = 4, NUM_MANIFOLDS = 50, POINTS_PER = 2 };
    enum { MAX_C = NUM_MANIFOLDS * POINTS_PER };

    phys_body_t bodies[NUM_BODIES];
    for (int i = 0; i < NUM_BODIES; ++i) { init_test_body(&bodies[i]); }

    phys_manifold_t manifolds[NUM_MANIFOLDS];
    phys_stab_hint_t hints[NUM_MANIFOLDS];
    for (int i = 0; i < NUM_MANIFOLDS; ++i) {
        init_test_manifold(&manifolds[i], i % 2, (i % 2) + 1, POINTS_PER);
        init_neutral_hint(&hints[i]);
    }

    /* Run 1. */
    phys_constraint_t out1[MAX_C];
    uint32_t count1 = 0;
    memset(out1, 0, sizeof(out1));

    phys_constraint_build_args_t args1 = {
        .manifolds          = manifolds,
        .hints              = hints,
        .manifold_count     = NUM_MANIFOLDS,
        .bodies             = bodies,
        .constraints_out    = out1,
        .constraint_count_out = &count1,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args1, &env.ctx);

    /* Run 2. */
    phys_constraint_t out2[MAX_C];
    uint32_t count2 = 0;
    memset(out2, 0, sizeof(out2));

    phys_constraint_build_args_t args2 = {
        .manifolds          = manifolds,
        .hints              = hints,
        .manifold_count     = NUM_MANIFOLDS,
        .bodies             = bodies,
        .constraints_out    = out2,
        .constraint_count_out = &count2,
        .max_constraints    = MAX_C,
        .dt                 = 1.0f / 60.0f,
        .baumgarte          = 0.2f,
        .slop               = 0.005f,
    };
    phys_stage_constraint_build_par(&args2, &env.ctx);

    /* Same count both runs. */
    ASSERT_EQ_UINT(count1, count2);
    ASSERT_EQ_UINT(NUM_MANIFOLDS * POINTS_PER, count1);

    env_teardown(&env);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_cb_identical_to_seq", test_par_cb_identical_to_seq},
    {"par_cb_batch_32",         test_par_cb_batch_32},
    {"par_cb_zero_manifolds",   test_par_cb_zero_manifolds},
    {"par_cb_single_manifold",  test_par_cb_single_manifold},
    {"par_cb_max_constraints",  test_par_cb_max_constraints},
    {"par_cb_deterministic",    test_par_cb_deterministic},
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
