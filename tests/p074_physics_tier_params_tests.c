/**
 * @file p074_physics_tier_params_tests.c
 * @brief Unit tests for per-tier solver parameters (phys-402).
 *
 * Tests cover: T0–T4 parameter lookup, cross-tier solver mode resolution.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"

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
        if (fabsf((float)(exp) - (float)(act)) > (float)(tol)) {               \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %f got %f (tol %f)\n",                            \
                    __FILE__, __LINE__, (double)(exp), (double)(act),            \
                    (double)(tol));                                              \
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

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * T0 → TGS solver, 3 substeps, 24 iterations.
 */
static int test_t0_params(void) {
    phys_tier_params_t p = phys_get_tier_params(PHYS_TIER_0_DIRECT);
    ASSERT_INT_EQ(PHYS_SOLVER_TGS, (int)p.solver_mode);
    ASSERT_INT_EQ(3,  (int)p.substeps);
    ASSERT_INT_EQ(24, (int)p.iterations);
    return 0;
}

/**
 * T1 → TGS solver, 2 substeps, 20 iterations.
 */
static int test_t1_params(void) {
    phys_tier_params_t p = phys_get_tier_params(PHYS_TIER_1_NEAR);
    ASSERT_INT_EQ(PHYS_SOLVER_TGS, (int)p.solver_mode);
    ASSERT_INT_EQ(2,  (int)p.substeps);
    ASSERT_INT_EQ(20, (int)p.iterations);
    return 0;
}

/**
 * T2 → XPBD solver, 1 substep, 8 iterations, compliance 1e-6.
 */
static int test_t2_params(void) {
    phys_tier_params_t p = phys_get_tier_params(PHYS_TIER_2_VISIBLE);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, (int)p.solver_mode);
    ASSERT_INT_EQ(1, (int)p.substeps);
    ASSERT_INT_EQ(8, (int)p.iterations);
    ASSERT_FLOAT_NEAR(1e-6f, p.compliance, 1e-9f);
    return 0;
}

/**
 * T3 → XPBD solver, 1 substep, 4 iterations, compliance 1e-5.
 */
static int test_t3_params(void) {
    phys_tier_params_t p = phys_get_tier_params(PHYS_TIER_3_WORLD);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, (int)p.solver_mode);
    ASSERT_INT_EQ(1, (int)p.substeps);
    ASSERT_INT_EQ(4, (int)p.iterations);
    ASSERT_FLOAT_NEAR(1e-5f, p.compliance, 1e-8f);
    return 0;
}

/**
 * T4 → XPBD solver, 1 substep, 2 iterations, compliance 1e-4.
 */
static int test_t4_params(void) {
    phys_tier_params_t p = phys_get_tier_params(PHYS_TIER_4_BACKGROUND);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, (int)p.solver_mode);
    ASSERT_INT_EQ(1, (int)p.substeps);
    ASSERT_INT_EQ(2, (int)p.iterations);
    ASSERT_FLOAT_NEAR(1e-4f, p.compliance, 1e-7f);
    return 0;
}

/**
 * Cross-tier: T0 + T3 → TGS (T0 is a high-fidelity tier).
 */
static int test_cross_tier_tgs_if_t0(void) {
    phys_solver_mode_t mode = phys_tier_cross_solver_mode(
        PHYS_TIER_0_DIRECT, PHYS_TIER_3_WORLD);
    ASSERT_INT_EQ(PHYS_SOLVER_TGS, (int)mode);
    return 0;
}

/**
 * Cross-tier: T2 + T3 → XPBD (neither is T0 or T1).
 */
static int test_cross_tier_xpbd_if_both_far(void) {
    phys_solver_mode_t mode = phys_tier_cross_solver_mode(
        PHYS_TIER_2_VISIBLE, PHYS_TIER_3_WORLD);
    ASSERT_INT_EQ(PHYS_SOLVER_XPBD, (int)mode);
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p074_physics_tier_params_tests\n");

    RUN_TEST(test_t0_params);
    RUN_TEST(test_t1_params);
    RUN_TEST(test_t2_params);
    RUN_TEST(test_t3_params);
    RUN_TEST(test_t4_params);
    RUN_TEST(test_cross_tier_tgs_if_t0);
    RUN_TEST(test_cross_tier_xpbd_if_both_far);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
