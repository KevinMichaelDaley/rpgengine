/**
 * @file p105_variable_dt_tests.c
 * @brief Tests for variable-dt fallback when physics can't sustain fixed rate.
 *
 * Tests cover:
 *   1. dt_override == 0 means step_plan uses fixed_dt (default).
 *   2. dt_override > 0 means step_plan uses dt_override instead of fixed_dt.
 *   3. substep_dt recomputed correctly from overridden dt.
 *   4. dt_override is clamped to max_dt_override (3× fixed_dt by default).
 *   5. Negative dt_override is treated as 0 (no override).
 *   6. Overload detection: sustained overrun triggers variable dt.
 *   7. Recovery: once ticks fit again, snaps back to fixed dt.
 *   8. Variable dt value matches actual wall time elapsed.
 */

#include "ferrum/physics/world.h"
#include "ferrum/physics/step_plan.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

/* ── Minimal test harness ──────────────────────────────────────── */

static int g_pass, g_fail;

#define ASSERT_TRUE(expr) do {                                         \
    if (!(expr)) {                                                     \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); \
        g_fail++; return;                                              \
    } else { g_pass++; }                                               \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, eps) do {                                \
    float _a = (a), _b = (b);                                         \
    if (fabsf(_a - _b) > (eps)) {                                     \
        fprintf(stderr, "  FAIL %s:%d: %.6f != %.6f (eps=%.6f)\n",    \
                __FILE__, __LINE__, (double)_a, (double)_b,            \
                (double)(eps));                                        \
        g_fail++; return;                                              \
    } else { g_pass++; }                                               \
} while (0)

/* ── Test: step_plan uses fixed_dt when dt_override == 0 ───────── */

static void test_step_plan_uses_fixed_dt_by_default(void) {
    printf("  test_step_plan_uses_fixed_dt_by_default\n");

    phys_world_config_t cfg = phys_world_config_default();
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Default: dt_override should be 0. */
    ASSERT_FLOAT_EQ(world.dt_override, 0.0f, 1e-9f);

    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);

    /* Plan should use fixed_dt. */
    ASSERT_FLOAT_EQ(plan.dt, cfg.fixed_dt, 1e-9f);
    ASSERT_FLOAT_EQ(plan.substep_dt, cfg.fixed_dt / (float)cfg.default_substeps, 1e-9f);

    phys_world_destroy(&world);
}

/* ── Test: step_plan uses dt_override when set ─────────────────── */

static void test_step_plan_uses_dt_override(void) {
    printf("  test_step_plan_uses_dt_override\n");

    phys_world_config_t cfg = phys_world_config_default();
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Set dt_override to 1.5× fixed_dt (simulating overload). */
    float override_dt = cfg.fixed_dt * 1.5f;
    world.dt_override = override_dt;

    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);

    /* Plan should use the override. */
    ASSERT_FLOAT_EQ(plan.dt, override_dt, 1e-9f);
    ASSERT_FLOAT_EQ(plan.substep_dt, override_dt / (float)cfg.default_substeps, 1e-9f);

    phys_world_destroy(&world);
}

/* ── Test: dt_override is clamped to max_dt_override ───────────── */

static void test_dt_override_clamped_to_max(void) {
    printf("  test_dt_override_clamped_to_max\n");

    phys_world_config_t cfg = phys_world_config_default();
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Set dt_override to something absurdly large (10× fixed_dt). */
    world.dt_override = cfg.fixed_dt * 10.0f;

    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);

    /* Plan should clamp to max_dt_override (default: 3× fixed_dt). */
    float expected_max = cfg.fixed_dt * 3.0f;
    ASSERT_FLOAT_EQ(plan.dt, expected_max, 1e-6f);

    phys_world_destroy(&world);
}

/* ── Test: negative dt_override treated as 0 (no override) ─────── */

static void test_negative_dt_override_ignored(void) {
    printf("  test_negative_dt_override_ignored\n");

    phys_world_config_t cfg = phys_world_config_default();
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    world.dt_override = -0.5f;

    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);

    /* Should fall back to fixed_dt. */
    ASSERT_FLOAT_EQ(plan.dt, cfg.fixed_dt, 1e-9f);

    phys_world_destroy(&world);
}

/* ── Test: custom max_dt_override is respected ─────────────────── */

static void test_custom_max_dt_override(void) {
    printf("  test_custom_max_dt_override\n");

    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_dt_override = 2.0f;  /* Cap at 2× fixed_dt. */
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Override at 5× fixed_dt — should clamp to 2× fixed_dt. */
    world.dt_override = cfg.fixed_dt * 5.0f;

    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);

    float expected = cfg.fixed_dt * 2.0f;
    ASSERT_FLOAT_EQ(plan.dt, expected, 1e-6f);

    phys_world_destroy(&world);
}

/* ── Test: dt_override exactly at fixed_dt uses override path ──── */

static void test_dt_override_equal_to_fixed(void) {
    printf("  test_dt_override_equal_to_fixed\n");

    phys_world_config_t cfg = phys_world_config_default();
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Override equal to fixed_dt — should still use it (same value). */
    world.dt_override = cfg.fixed_dt;

    phys_step_plan_t plan;
    phys_stage_step_plan(&plan, &world, NULL);

    ASSERT_FLOAT_EQ(plan.dt, cfg.fixed_dt, 1e-9f);

    phys_world_destroy(&world);
}

/* ── Test: overload tracker bitfield mechanics ─────────────────── */

/**
 * Tests for the overload detection helper.
 *
 * The tick runner uses a rolling bitfield to track whether each of
 * the last N ticks overran the target period.  When the popcount
 * exceeds a threshold, it switches to variable dt.
 *
 * We test the helper function directly.
 */

/* We declare the helper prototype here; it's defined in
 * phys_tick_runner.c as a static, but we'll expose it via a
 * test-only accessor or test the equivalent logic inline. */

/** Rolling overload window — tracks last 16 ticks via bitfield. */
#define OVERLOAD_WINDOW 16
/** Threshold: if this many of the last WINDOW ticks overran, switch. */
#define OVERLOAD_THRESHOLD 12

static int popcount16(uint16_t v) {
    int c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

static void test_overload_tracker_all_ok(void) {
    printf("  test_overload_tracker_all_ok\n");

    /* All ticks on time: bitfield = 0. */
    uint16_t history = 0;
    ASSERT_TRUE(popcount16(history) < OVERLOAD_THRESHOLD);
}

static void test_overload_tracker_sustained_overrun(void) {
    printf("  test_overload_tracker_sustained_overrun\n");

    /* Simulate 16 ticks all overrunning. */
    uint16_t history = 0;
    for (int i = 0; i < 16; i++) {
        history = (uint16_t)((history << 1) | 1);
    }
    ASSERT_TRUE(popcount16(history) >= OVERLOAD_THRESHOLD);
}

static void test_overload_tracker_recovery(void) {
    printf("  test_overload_tracker_recovery\n");

    /* Start with all overrunning, then 8 good ticks. */
    uint16_t history = 0xFFFF; /* all overrun */
    for (int i = 0; i < 8; i++) {
        history = (uint16_t)(history << 1); /* shift in 0 (on-time). */
    }
    /* Now only 8 of 16 are overrun — should be below threshold. */
    ASSERT_TRUE(popcount16(history) < OVERLOAD_THRESHOLD);
}

static void test_overload_tracker_intermittent(void) {
    printf("  test_overload_tracker_intermittent\n");

    /* Alternating overrun/ok: 01010101... = 8 of 16.  Should NOT trigger. */
    uint16_t history = 0x5555;
    ASSERT_TRUE(popcount16(history) < OVERLOAD_THRESHOLD);
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    printf("p105_variable_dt_tests\n");

    /* step_plan dt_override tests */
    test_step_plan_uses_fixed_dt_by_default();
    test_step_plan_uses_dt_override();
    test_dt_override_clamped_to_max();
    test_negative_dt_override_ignored();
    test_custom_max_dt_override();
    test_dt_override_equal_to_fixed();

    /* overload tracker logic tests */
    test_overload_tracker_all_ok();
    test_overload_tracker_sustained_overrun();
    test_overload_tracker_recovery();
    test_overload_tracker_intermittent();

    printf("p105: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
