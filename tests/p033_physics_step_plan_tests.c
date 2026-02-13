#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/world.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                          \
    do {                                                                                                 \
        if ((exp) != (act)) {                                                                            \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,     \
                    (int)(exp), (int)(act));                                                              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                                                 \
    do {                                                                                                 \
        float _e = (float)(exp);                                                                         \
        float _a = (float)(act);                                                                         \
        if (fabsf(_e - _a) > (eps)) {                                                                    \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f got %f (eps=%f)\n", __FILE__,  \
                    __LINE__, (double)_e, (double)_a, (double)(eps));                                     \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test that step plan picks up substeps, iterations, dt, and substep_dt
 * from world config.
 */
static int test_step_plan_defaults(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.default_substeps = 2;
    cfg.default_solver_iterations = 8;
    cfg.fixed_dt = 1.0f / 30.0f;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    phys_step_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    phys_stage_step_plan(&plan, &world, NULL);

    ASSERT_INT_EQ(2, (int)plan.substeps);
    ASSERT_INT_EQ(8, (int)plan.solver_iterations);
    ASSERT_FLOAT_NEAR(1.0f / 30.0f, plan.dt, 0.0001f);
    ASSERT_FLOAT_NEAR(1.0f / 60.0f, plan.substep_dt, 0.0001f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test that tiers T0–T4 are active, T5 (sleeping) is inactive.
 */
static int test_step_plan_all_tiers_active(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.default_substeps = 2;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    phys_step_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    phys_stage_step_plan(&plan, &world, NULL);

    /* T0–T4 should be active with default params. */
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        if (t == PHYS_TIER_5_SLEEPING) {
            ASSERT_TRUE(!plan.tier_params[t].active);
            continue;
        }
        ASSERT_TRUE(plan.tier_params[t].active);
        ASSERT_INT_EQ((int)cfg.default_substeps, (int)plan.tier_params[t].substeps);
        ASSERT_INT_EQ((int)cfg.default_solver_iterations, (int)plan.tier_params[t].iterations);
        ASSERT_FLOAT_NEAR(1.0f, plan.tier_params[t].friction_boost, 0.0001f);
        ASSERT_FLOAT_NEAR(1.0f, plan.tier_params[t].restitution_scale, 0.0001f);
    }

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test that substep_dt = dt / substeps for various substep counts.
 */
static int test_step_plan_substep_dt(void) {
    phys_world_config_t cfg = phys_world_config_default();

    /* Test with 4 substeps. */
    cfg.default_substeps = 4;
    cfg.fixed_dt = 1.0f / 60.0f;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    phys_step_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    phys_stage_step_plan(&plan, &world, NULL);

    ASSERT_INT_EQ(4, (int)plan.substeps);
    ASSERT_FLOAT_NEAR(1.0f / 60.0f, plan.dt, 0.0001f);
    ASSERT_FLOAT_NEAR(1.0f / 240.0f, plan.substep_dt, 0.0001f);

    phys_world_destroy(&world);

    /* Test with 1 substep — substep_dt should equal dt. */
    cfg.default_substeps = 1;
    phys_world_init(&world, &cfg);
    memset(&plan, 0, sizeof(plan));
    phys_stage_step_plan(&plan, &world, NULL);

    ASSERT_INT_EQ(1, (int)plan.substeps);
    ASSERT_FLOAT_NEAR(plan.dt, plan.substep_dt, 0.0001f);

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test that NULL game state works without crashing and produces
 * the same results as the defaults test.
 */
static int test_step_plan_null_game_state(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.default_substeps = 2;
    cfg.default_solver_iterations = 10;
    cfg.fixed_dt = 1.0f / 30.0f;

    phys_world_t world;
    phys_world_init(&world, &cfg);

    phys_step_plan_t plan;
    memset(&plan, 0, sizeof(plan));

    /* Explicitly pass NULL game state. */
    phys_stage_step_plan(&plan, &world, NULL);

    ASSERT_INT_EQ(2, (int)plan.substeps);
    ASSERT_INT_EQ(10, (int)plan.solver_iterations);
    ASSERT_FLOAT_NEAR(1.0f / 30.0f, plan.dt, 0.0001f);
    ASSERT_FLOAT_NEAR(1.0f / 60.0f, plan.substep_dt, 0.0001f);

    /* Tiers T0–T4 should still be active; T5 inactive. */
    for (int t = 0; t < PHYS_TIER_COUNT; ++t) {
        if (t == PHYS_TIER_5_SLEEPING) {
            ASSERT_TRUE(!plan.tier_params[t].active);
        } else {
            ASSERT_TRUE(plan.tier_params[t].active);
        }
    }

    phys_world_destroy(&world);
    return 0;
}

/**
 * Test that NULL plan and NULL world don't crash.
 */
static int test_step_plan_null_safe(void) {
    phys_world_config_t cfg = phys_world_config_default();
    phys_world_t world;
    phys_world_init(&world, &cfg);

    /* NULL plan — should not crash. */
    phys_stage_step_plan(NULL, &world, NULL);

    /* NULL world — should not crash. */
    phys_step_plan_t plan;
    memset(&plan, 0, sizeof(plan));
    phys_stage_step_plan(&plan, NULL, NULL);

    /* Plan should remain zeroed when world is NULL. */
    ASSERT_INT_EQ(0, (int)plan.substeps);

    /* Both NULL — should not crash. */
    phys_stage_step_plan(NULL, NULL, NULL);

    phys_world_destroy(&world);
    return 0;
}

/* ── Test runner ─────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"step_plan_defaults",          test_step_plan_defaults},
    {"step_plan_all_tiers_active",  test_step_plan_all_tiers_active},
    {"step_plan_substep_dt",        test_step_plan_substep_dt},
    {"step_plan_null_game_state",   test_step_plan_null_game_state},
    {"step_plan_null_safe",         test_step_plan_null_safe},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
