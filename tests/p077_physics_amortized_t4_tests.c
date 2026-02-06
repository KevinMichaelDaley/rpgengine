/**
 * @file p077_physics_amortized_t4_tests.c
 * @brief Unit tests for T4 amortized ticking and visual interpolation.
 *
 * T4 bodies only tick every 3rd frame.  Between physics ticks their
 * visual pose is interpolated from the previous snapshot.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/amortized.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"
#include "ferrum/physics/world.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

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
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static const float TOL = 1e-4f;

/** Build a body at position (px,py,pz) with identity orientation and tier. */
static phys_body_t make_body(float px, float py, float pz, uint8_t tier)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position    = (phys_vec3_t){px, py, pz};
    b.orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    b.tier        = tier;
    return b;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/** 1. Init / destroy lifecycle. */
static int test_amortized_init_destroy(void)
{
    phys_amortized_state_t state;
    bool ok = phys_amortized_init(&state, 16);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(state.body_capacity == 16);
    ASSERT_TRUE(state.prev_positions != NULL);
    ASSERT_TRUE(state.prev_orientations != NULL);
    ASSERT_TRUE(state.last_tick_frame == 0);

    phys_amortized_destroy(&state);
    ASSERT_TRUE(state.prev_positions == NULL);
    ASSERT_TRUE(state.prev_orientations == NULL);
    ASSERT_TRUE(state.body_capacity == 0);
    return 0;
}

/** 2. T4 active every 3rd frame via step plan. */
static int test_t4_active_every_3rd_frame(void)
{
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 4;
    int rc = phys_world_init(&world, &cfg);
    ASSERT_TRUE(rc == 0);

    /* tick_count 0: T4 active (0 % 3 == 0). */
    world.tick_count = 0;
    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_TRUE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    /* tick_count 1: T4 inactive. */
    world.tick_count = 1;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_FALSE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    /* tick_count 2: T4 inactive. */
    world.tick_count = 2;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_FALSE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    /* tick_count 3: T4 active. */
    world.tick_count = 3;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_TRUE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    /* tick_count 4: T4 inactive. */
    world.tick_count = 4;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_FALSE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    /* tick_count 5: T4 inactive. */
    world.tick_count = 5;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_FALSE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    /* tick_count 6: T4 active. */
    world.tick_count = 6;
    phys_stage_step_plan(&plan, &world, NULL);
    ASSERT_TRUE(plan.tier_params[PHYS_TIER_4_BACKGROUND].active);

    phys_world_destroy(&world);
    return 0;
}

/** 3. Snapshot stores previous pose for T4 bodies. */
static int test_snapshot_stores_prev_pose(void)
{
    phys_amortized_state_t state;
    ASSERT_TRUE(phys_amortized_init(&state, 4));

    phys_body_t bodies[2];
    bodies[0] = make_body(1.0f, 2.0f, 3.0f, PHYS_TIER_4_BACKGROUND);
    bodies[1] = make_body(4.0f, 5.0f, 6.0f, PHYS_TIER_4_BACKGROUND);

    /* Snapshot on frame 0 (a T4 tick frame). */
    phys_amortized_snapshot(&state, bodies, 2, 0);

    ASSERT_FLOAT_NEAR(1.0f, state.prev_positions[0].x, TOL);
    ASSERT_FLOAT_NEAR(2.0f, state.prev_positions[0].y, TOL);
    ASSERT_FLOAT_NEAR(3.0f, state.prev_positions[0].z, TOL);
    ASSERT_FLOAT_NEAR(4.0f, state.prev_positions[1].x, TOL);
    ASSERT_FLOAT_NEAR(5.0f, state.prev_positions[1].y, TOL);
    ASSERT_FLOAT_NEAR(6.0f, state.prev_positions[1].z, TOL);
    ASSERT_TRUE(state.last_tick_frame == 0);

    phys_amortized_destroy(&state);
    return 0;
}

/** 4. On T4 tick frame, visual == current pose (alpha 0). */
static int test_interpolate_alpha_zero(void)
{
    phys_amortized_state_t state;
    ASSERT_TRUE(phys_amortized_init(&state, 4));

    /* Snapshot previous pose at frame 0. */
    phys_body_t prev_bodies[1];
    prev_bodies[0] = make_body(0.0f, 0.0f, 0.0f, PHYS_TIER_4_BACKGROUND);
    phys_amortized_snapshot(&state, prev_bodies, 1, 0);

    /* Bodies moved to new position by frame 3 (next T4 tick). */
    phys_body_t bodies[1];
    bodies[0] = make_body(9.0f, 9.0f, 9.0f, PHYS_TIER_4_BACKGROUND);

    /* Snapshot again at frame 3 — this updates prev. */
    phys_amortized_snapshot(&state, bodies, 1, 3);

    /* Interpolate at frame 3 (current_frame == last_tick_frame). */
    phys_vec3_t vpos[1];
    phys_quat_t vrot[1];
    phys_amortized_interpolate(&state, bodies, 1, 3, vpos, vrot);

    /* alpha = (3 - 3) / 3 = 0 → visual = prev = current (just snapped). */
    ASSERT_FLOAT_NEAR(9.0f, vpos[0].x, TOL);
    ASSERT_FLOAT_NEAR(9.0f, vpos[0].y, TOL);
    ASSERT_FLOAT_NEAR(9.0f, vpos[0].z, TOL);

    phys_amortized_destroy(&state);
    return 0;
}

/** 5. On frame+1, visual is 1/3 interpolation. */
static int test_interpolate_alpha_mid(void)
{
    phys_amortized_state_t state;
    ASSERT_TRUE(phys_amortized_init(&state, 4));

    /* Previous positions from frame 0. */
    phys_body_t snap[1];
    snap[0] = make_body(0.0f, 0.0f, 0.0f, PHYS_TIER_4_BACKGROUND);
    phys_amortized_snapshot(&state, snap, 1, 0);

    /* Current bodies at frame 1 have moved (but T4 wasn't ticked). */
    phys_body_t bodies[1];
    bodies[0] = make_body(3.0f, 6.0f, 9.0f, PHYS_TIER_4_BACKGROUND);

    /* Interpolate at frame 1: alpha = (1 - 0) / 3 = 0.333... */
    phys_vec3_t vpos[1];
    phys_quat_t vrot[1];
    phys_amortized_interpolate(&state, bodies, 1, 1, vpos, vrot);

    /* lerp(0, 3, 1/3) = 1.0 */
    ASSERT_FLOAT_NEAR(1.0f, vpos[0].x, TOL);
    ASSERT_FLOAT_NEAR(2.0f, vpos[0].y, TOL);
    ASSERT_FLOAT_NEAR(3.0f, vpos[0].z, TOL);

    phys_amortized_destroy(&state);
    return 0;
}

/** 6. On frame+2, visual is 2/3 interpolation. */
static int test_interpolate_alpha_two_thirds(void)
{
    phys_amortized_state_t state;
    ASSERT_TRUE(phys_amortized_init(&state, 4));

    /* Previous positions from frame 0. */
    phys_body_t snap[1];
    snap[0] = make_body(0.0f, 0.0f, 0.0f, PHYS_TIER_4_BACKGROUND);
    phys_amortized_snapshot(&state, snap, 1, 0);

    /* Current bodies at frame 2. */
    phys_body_t bodies[1];
    bodies[0] = make_body(3.0f, 6.0f, 9.0f, PHYS_TIER_4_BACKGROUND);

    /* Interpolate at frame 2: alpha = (2 - 0) / 3 = 0.666... */
    phys_vec3_t vpos[1];
    phys_quat_t vrot[1];
    phys_amortized_interpolate(&state, bodies, 1, 2, vpos, vrot);

    /* lerp(0, 3, 2/3) = 2.0 */
    ASSERT_FLOAT_NEAR(2.0f, vpos[0].x, TOL);
    ASSERT_FLOAT_NEAR(4.0f, vpos[0].y, TOL);
    ASSERT_FLOAT_NEAR(6.0f, vpos[0].z, TOL);

    phys_amortized_destroy(&state);
    return 0;
}

/** 7. Non-T4 bodies are not affected by amortized interpolation. */
static int test_non_t4_bodies_untouched(void)
{
    phys_amortized_state_t state;
    ASSERT_TRUE(phys_amortized_init(&state, 4));

    /* Bodies: T0, T1, T2, T3 — none are T4. */
    phys_body_t bodies[4];
    bodies[0] = make_body(1.0f, 2.0f, 3.0f, PHYS_TIER_0_DIRECT);
    bodies[1] = make_body(4.0f, 5.0f, 6.0f, PHYS_TIER_1_NEAR);
    bodies[2] = make_body(7.0f, 8.0f, 9.0f, PHYS_TIER_2_VISIBLE);
    bodies[3] = make_body(10.0f, 11.0f, 12.0f, PHYS_TIER_3_WORLD);

    /* Snapshot at frame 0. */
    phys_amortized_snapshot(&state, bodies, 4, 0);

    /* Move bodies for interpolation test. */
    bodies[0].position = (phys_vec3_t){100.0f, 200.0f, 300.0f};
    bodies[1].position = (phys_vec3_t){400.0f, 500.0f, 600.0f};
    bodies[2].position = (phys_vec3_t){700.0f, 800.0f, 900.0f};
    bodies[3].position = (phys_vec3_t){10.0f, 11.0f, 12.0f};

    /* Interpolate at frame 1 (non-T4-tick frame). */
    phys_vec3_t vpos[4];
    phys_quat_t vrot[4];
    phys_amortized_interpolate(&state, bodies, 4, 1, vpos, vrot);

    /* Non-T4 bodies should have their current position copied verbatim. */
    ASSERT_FLOAT_NEAR(100.0f, vpos[0].x, TOL);
    ASSERT_FLOAT_NEAR(200.0f, vpos[0].y, TOL);
    ASSERT_FLOAT_NEAR(300.0f, vpos[0].z, TOL);

    ASSERT_FLOAT_NEAR(400.0f, vpos[1].x, TOL);
    ASSERT_FLOAT_NEAR(500.0f, vpos[1].y, TOL);
    ASSERT_FLOAT_NEAR(600.0f, vpos[1].z, TOL);

    ASSERT_FLOAT_NEAR(700.0f, vpos[2].x, TOL);
    ASSERT_FLOAT_NEAR(800.0f, vpos[2].y, TOL);
    ASSERT_FLOAT_NEAR(900.0f, vpos[2].z, TOL);

    ASSERT_FLOAT_NEAR(10.0f, vpos[3].x, TOL);
    ASSERT_FLOAT_NEAR(11.0f, vpos[3].y, TOL);
    ASSERT_FLOAT_NEAR(12.0f, vpos[3].z, TOL);

    phys_amortized_destroy(&state);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p077_physics_amortized_t4_tests\n");
    RUN_TEST(test_amortized_init_destroy);
    RUN_TEST(test_t4_active_every_3rd_frame);
    RUN_TEST(test_snapshot_stores_prev_pose);
    RUN_TEST(test_interpolate_alpha_zero);
    RUN_TEST(test_interpolate_alpha_mid);
    RUN_TEST(test_interpolate_alpha_two_thirds);
    RUN_TEST(test_non_t4_bodies_untouched);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count > 0 ? 1 : 0;
}
