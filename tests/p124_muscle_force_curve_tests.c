/**
 * @file p124_muscle_force_curve_tests.c
 * @brief Tests for Hill-type muscle force model.
 */

#include <math.h>
#include <stdio.h>

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

/* Test: init sets reasonable defaults. */
static int test_params_init(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    ASSERT_TRUE(p.optimal_length > 0.0f);
    ASSERT_TRUE(p.max_force > 0.0f);
    ASSERT_TRUE(p.max_velocity > 0.0f);
    ASSERT_TRUE(p.width > 0.0f);
    return 0;
}

/* Test: active force-length peaks at optimal length (norm_length=1). */
static int test_active_peak_at_optimal(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t out;

    phys_muscle_force_compute(&p, 1.0f, 1.0f, 0.0f, &out);
    ASSERT_NEAR(1.0f, out.f_active, 0.01f);

    /* At 0.5 and 1.5 optimal length, active force should be lower. */
    phys_muscle_force_t out_short, out_long;
    phys_muscle_force_compute(&p, 1.0f, 0.5f, 0.0f, &out_short);
    phys_muscle_force_compute(&p, 1.0f, 1.5f, 0.0f, &out_long);
    ASSERT_TRUE(out_short.f_active < out.f_active);
    ASSERT_TRUE(out_long.f_active < out.f_active);
    return 0;
}

/* Test: passive force is zero below optimal length. */
static int test_passive_zero_below_optimal(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t out;

    phys_muscle_force_compute(&p, 0.0f, 0.8f, 0.0f, &out);
    ASSERT_NEAR(0.0f, out.f_passive, 1e-6f);
    return 0;
}

/* Test: passive force rises at long lengths. */
static int test_passive_rises_at_long_length(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t out1, out2;

    phys_muscle_force_compute(&p, 0.0f, 1.2f, 0.0f, &out1);
    phys_muscle_force_compute(&p, 0.0f, 1.5f, 0.0f, &out2);
    ASSERT_TRUE(out2.f_passive > out1.f_passive);
    ASSERT_TRUE(out1.f_passive > 0.0f);
    return 0;
}

/* Test: force-velocity: isometric (V=0) gives multiplier ~1. */
static int test_isometric_velocity(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t out;

    phys_muscle_force_compute(&p, 1.0f, 1.0f, 0.0f, &out);
    ASSERT_NEAR(1.0f, out.f_velocity, 0.01f);
    return 0;
}

/* Test: force decreases with shortening velocity. */
static int test_force_drops_with_shortening(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t slow, fast;

    phys_muscle_force_compute(&p, 1.0f, 1.0f, 0.3f, &slow);
    phys_muscle_force_compute(&p, 1.0f, 1.0f, 0.8f, &fast);
    ASSERT_TRUE(fast.f_velocity < slow.f_velocity);
    return 0;
}

/* Test: eccentric force exceeds isometric. */
static int test_eccentric_exceeds_isometric(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t iso, ecc;

    phys_muscle_force_compute(&p, 1.0f, 1.0f, 0.0f, &iso);
    phys_muscle_force_compute(&p, 1.0f, 1.0f, -0.5f, &ecc);
    ASSERT_TRUE(ecc.f_velocity > iso.f_velocity);
    return 0;
}

/* Test: zero activation yields only passive force. */
static int test_zero_activation_passive_only(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t out;

    /* At norm_length=1.3 with zero activation: only passive. */
    phys_muscle_force_compute(&p, 0.0f, 1.3f, 0.0f, &out);
    ASSERT_TRUE(out.f_total > 0.0f); /* passive contribution */

    /* At optimal length with zero activation: f_total = passive only. */
    phys_muscle_force_compute(&p, 0.0f, 1.0f, 0.0f, &out);
    ASSERT_NEAR(0.0f, out.f_total, 1e-6f); /* passive is zero at L=1 */
    return 0;
}

/* Test: max force at optimal length, full activation, isometric. */
static int test_max_force_value(void) {
    phys_muscle_params_t p;
    phys_muscle_params_init(&p);
    phys_muscle_force_t out;

    phys_muscle_force_compute(&p, 1.0f, 1.0f, 0.0f, &out);
    /* Should be approximately max_force (100 N default). */
    ASSERT_NEAR(p.max_force, out.f_total, 1.0f);
    return 0;
}

/* Test: NULL pointers are no-ops. */
static int test_null_pointers(void) {
    phys_muscle_params_init(NULL);
    phys_muscle_force_compute(NULL, 1.0f, 1.0f, 0.0f, NULL);
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
    printf("p124_muscle_force_curve_tests:\n");

    RUN_TEST(test_params_init);
    RUN_TEST(test_active_peak_at_optimal);
    RUN_TEST(test_passive_zero_below_optimal);
    RUN_TEST(test_passive_rises_at_long_length);
    RUN_TEST(test_isometric_velocity);
    RUN_TEST(test_force_drops_with_shortening);
    RUN_TEST(test_eccentric_exceeds_isometric);
    RUN_TEST(test_zero_activation_passive_only);
    RUN_TEST(test_max_force_value);
    RUN_TEST(test_null_pointers);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
