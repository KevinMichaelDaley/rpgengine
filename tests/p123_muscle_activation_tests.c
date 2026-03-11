/**
 * @file p123_muscle_activation_tests.c
 * @brief Tests for muscle activation dynamics.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/muscle/activation.h"

#define ASSERT_TRUE(cond)                                               \
    do { if (!(cond)) {                                                 \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1;                                                       \
    } } while (0)

#define ASSERT_NEAR(exp, act, tol)                                      \
    do { if (fabsf((float)(exp) - (float)(act)) > (float)(tol)) {      \
        fprintf(stderr, "FAIL: %s:%d: expected %.6f got %.6f (tol %.6f)\n", \
                __FILE__, __LINE__, (float)(exp), (float)(act), (float)(tol)); \
        return 1;                                                       \
    } } while (0)

/* Test: init sets safe defaults. */
static int test_init_defaults(void) {
    phys_muscle_activation_t act;
    phys_muscle_activation_init(&act);
    ASSERT_NEAR(0.0f, act.activation, 1e-6f);
    ASSERT_NEAR(0.0f, act.excitation, 1e-6f);
    ASSERT_TRUE(act.tau_rise > 0.0f);
    ASSERT_TRUE(act.tau_fall > 0.0f);
    ASSERT_TRUE(act.tau_fall > act.tau_rise);
    return 0;
}

/* Test: NULL init is no-op. */
static int test_init_null(void) {
    phys_muscle_activation_init(NULL); /* Should not crash. */
    return 0;
}

/* Test: step with u > a uses tau_rise (fast rise). */
static int test_step_rising(void) {
    phys_muscle_activation_t act;
    phys_muscle_activation_init(&act);
    act.excitation = 1.0f;
    act.activation = 0.0f;

    /* After one step at dt=0.015 (= tau_rise), activation should
     * reach ~0.5 (semi-implicit Euler). */
    phys_muscle_activation_step(&act, 0.015f);
    ASSERT_TRUE(act.activation > 0.3f);
    ASSERT_TRUE(act.activation < 0.7f);
    return 0;
}

/* Test: step with u < a uses tau_fall (slow fall). */
static int test_step_falling(void) {
    phys_muscle_activation_t act;
    phys_muscle_activation_init(&act);
    act.activation = 1.0f;
    act.excitation = 0.0f;

    /* After one step at dt=0.050 (= tau_fall), should drop to ~0.5. */
    phys_muscle_activation_step(&act, 0.050f);
    ASSERT_TRUE(act.activation > 0.3f);
    ASSERT_TRUE(act.activation < 0.7f);
    return 0;
}

/* Test: activation is clamped to [0,1]. */
static int test_clamp(void) {
    phys_muscle_activation_t act;
    phys_muscle_activation_init(&act);
    act.excitation = 1.0f;
    act.activation = 0.0f;

    /* Many steps should converge to 1.0, never exceed it. */
    for (int i = 0; i < 1000; i++) {
        phys_muscle_activation_step(&act, 0.001f);
    }
    ASSERT_TRUE(act.activation <= 1.0f);
    ASSERT_TRUE(act.activation >= 0.99f);
    return 0;
}

/* Test: zero dt is no-op. */
static int test_zero_dt(void) {
    phys_muscle_activation_t act;
    phys_muscle_activation_init(&act);
    act.activation = 0.5f;
    act.excitation = 1.0f;

    phys_muscle_activation_step(&act, 0.0f);
    ASSERT_NEAR(0.5f, act.activation, 1e-6f);
    return 0;
}

/* Test: convergence to target excitation. */
static int test_convergence(void) {
    phys_muscle_activation_t act;
    phys_muscle_activation_init(&act);
    act.excitation = 0.7f;

    for (int i = 0; i < 10000; i++) {
        phys_muscle_activation_step(&act, 0.001f);
    }
    ASSERT_NEAR(0.7f, act.activation, 0.001f);
    return 0;
}

/* Test: fall is slower than rise. */
static int test_asymmetric_dynamics(void) {
    phys_muscle_activation_t rise;
    phys_muscle_activation_init(&rise);
    rise.excitation = 1.0f;

    phys_muscle_activation_t fall;
    phys_muscle_activation_init(&fall);
    fall.activation = 1.0f;
    fall.excitation = 0.0f;

    /* After same dt, rise should be further from start than fall. */
    float dt = 0.010f;
    phys_muscle_activation_step(&rise, dt);
    phys_muscle_activation_step(&fall, dt);

    float rise_delta = rise.activation; /* started at 0 */
    float fall_delta = 1.0f - fall.activation; /* started at 1 */

    /* Rise should be faster (larger change) than fall. */
    ASSERT_TRUE(rise_delta > fall_delta);
    return 0;
}

/* ── Runner ──────────────────────────────────────────────────────── */

#define RUN_TEST(fn) do {                                               \
    printf("  %-60s", #fn);                                             \
    int _r = fn();                                                      \
    printf("%s\n", _r ? "FAIL" : "PASS");                              \
    if (_r) fail_count++;                                               \
    test_count++;                                                       \
} while (0)

int main(void) {
    int fail_count = 0, test_count = 0;
    printf("p123_muscle_activation_tests:\n");

    RUN_TEST(test_init_defaults);
    RUN_TEST(test_init_null);
    RUN_TEST(test_step_rising);
    RUN_TEST(test_step_falling);
    RUN_TEST(test_clamp);
    RUN_TEST(test_zero_dt);
    RUN_TEST(test_convergence);
    RUN_TEST(test_asymmetric_dynamics);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
