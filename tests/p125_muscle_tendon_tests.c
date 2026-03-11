/**
 * @file p125_muscle_tendon_tests.c
 * @brief Tests for tendon series elastic element.
 */

#include <math.h>
#include <stdio.h>

#include "ferrum/physics/muscle/tendon.h"
#include "ferrum/physics/muscle/force_curve.h"

#define ASSERT_TRUE(cond)                                               \
    do { if (!(cond)) {                                                 \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1;                                                       \
    } } while (0)

#define ASSERT_NEAR(exp, act, tol)                                      \
    do { if (fabsf((float)(exp) - (float)(act)) > (float)(tol)) {      \
        fprintf(stderr, "FAIL: %s:%d: expected %.6f got %.6f\n",       \
                __FILE__, __LINE__, (float)(exp), (float)(act));        \
        return 1;                                                       \
    } } while (0)

/* Test: init sets defaults. */
static int test_tendon_init(void) {
    phys_tendon_params_t t;
    phys_tendon_params_init(&t);
    ASSERT_TRUE(t.slack_length > 0.0f);
    ASSERT_TRUE(t.stiffness > 0.0f);
    ASSERT_TRUE(t.reference_strain > 0.0f);
    return 0;
}

/* Test: NULL init is no-op. */
static int test_tendon_init_null(void) {
    phys_tendon_params_init(NULL);
    return 0;
}

/* Test: slack tendon produces zero force. */
static int test_slack_tendon_zero_force(void) {
    phys_tendon_params_t t;
    phys_tendon_params_init(&t);
    phys_muscle_params_t m;
    phys_muscle_params_init(&m);

    phys_tendon_state_t state;
    /* Total length less than fiber + slack → tendon slack. */
    float total = m.optimal_length + t.slack_length * 0.8f;
    phys_tendon_equilibrium(&t, &m, 0.0f, total, m.optimal_length, &state);
    ASSERT_NEAR(0.0f, state.tendon_force, 1.0f);
    return 0;
}

/* Test: stretched tendon produces positive force. */
static int test_stretched_tendon_positive_force(void) {
    phys_tendon_params_t t;
    phys_tendon_params_init(&t);
    phys_muscle_params_t m;
    phys_muscle_params_init(&m);

    phys_tendon_state_t state;
    /* Total length well beyond fiber + slack → tendon under tension. */
    float total = m.optimal_length + t.slack_length * 1.5f;
    phys_tendon_equilibrium(&t, &m, 0.5f, total, m.optimal_length, &state);
    ASSERT_TRUE(state.tendon_force > 0.0f);
    return 0;
}

/* Test: fiber + tendon lengths sum to total (accounting for pennation). */
static int test_length_conservation(void) {
    phys_tendon_params_t t;
    phys_tendon_params_init(&t);
    phys_muscle_params_t m;
    phys_muscle_params_init(&m);

    phys_tendon_state_t state;
    float total = m.optimal_length + t.slack_length * 1.2f;
    phys_tendon_equilibrium(&t, &m, 0.5f, total, m.optimal_length, &state);

    float cos_penn = cosf(m.pennation_angle);
    float reconstructed = state.fiber_length * cos_penn + state.tendon_length;
    ASSERT_NEAR(total, reconstructed, 0.01f);
    return 0;
}

/* Test: NULL pointers are no-ops. */
static int test_null_pointers(void) {
    phys_tendon_params_t t;
    phys_tendon_params_init(&t);
    phys_tendon_state_t state;

    phys_tendon_equilibrium(NULL, NULL, 0.0f, 0.0f, 0.0f, NULL);
    phys_tendon_equilibrium(&t, NULL, 0.0f, 0.0f, 0.0f, &state);
    return 0;
}

/* Test: fiber length stays in valid range. */
static int test_fiber_length_valid_range(void) {
    phys_tendon_params_t t;
    phys_tendon_params_init(&t);
    phys_muscle_params_t m;
    phys_muscle_params_init(&m);
    phys_tendon_state_t state;

    /* Very short total length. */
    phys_tendon_equilibrium(&t, &m, 1.0f, 0.05f, m.optimal_length, &state);
    ASSERT_TRUE(state.fiber_length > 0.0f);

    /* Very long total length. */
    phys_tendon_equilibrium(&t, &m, 1.0f, 2.0f, m.optimal_length, &state);
    ASSERT_TRUE(state.fiber_length > 0.0f);
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
    printf("p125_muscle_tendon_tests:\n");

    RUN_TEST(test_tendon_init);
    RUN_TEST(test_tendon_init_null);
    RUN_TEST(test_slack_tendon_zero_force);
    RUN_TEST(test_stretched_tendon_positive_force);
    RUN_TEST(test_length_conservation);
    RUN_TEST(test_null_pointers);
    RUN_TEST(test_fiber_length_valid_range);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
