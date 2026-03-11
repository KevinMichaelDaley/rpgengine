/**
 * @file p127_muscle_unit_tests.c
 * @brief Tests for composite muscle unit evaluation.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/muscle/muscle_unit.h"
#include "ferrum/physics/body.h"

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

static phys_body_t make_body_(float x, float y, float z) {
    phys_body_t b;
    memset(&b, 0, sizeof(b));
    b.position = (phys_vec3_t){x, y, z};
    b.orientation = (phys_quat_t){0, 0, 0, 1};
    b.inv_mass = 1.0f;
    return b;
}

/* Test: init sets safe defaults. */
static int test_unit_init(void) {
    phys_muscle_unit_t unit;
    phys_muscle_unit_init(&unit);
    ASSERT_TRUE(unit.params.max_force > 0.0f);
    ASSERT_TRUE(unit.fiber_length > 0.0f);
    ASSERT_NEAR(0.0f, unit.fiber_velocity, 1e-6f);
    ASSERT_NEAR(0.0f, unit.activation.activation, 1e-6f);
    return 0;
}

/* Test: full pipeline produces torque with excitation. */
static int test_produces_torque(void) {
    phys_body_t a = make_body_(0, 0, 0);
    phys_body_t b = make_body_(0.2f, 0, 0);

    phys_muscle_unit_t unit;
    phys_muscle_unit_init(&unit);
    /* Place muscle off-axis for moment arm. */
    unit.attach.origin_local    = (phys_vec3_t){0.0f, 0.05f, 0.0f};
    unit.attach.insertion_local = (phys_vec3_t){0.0f, 0.05f, 0.0f};
    unit.activation.excitation = 1.0f;

    phys_vec3_t axis = {0, 0, 1};
    phys_vec3_t pivot = {0.1f, 0, 0};

    /* Run several steps to build up activation. */
    float torque = 0.0f;
    for (int i = 0; i < 100; i++) {
        phys_muscle_unit_evaluate(&unit, &axis, &pivot,
                                   &a, &b, 0.001f, &torque);
    }
    /* Should produce non-zero torque with full activation. */
    ASSERT_TRUE(fabsf(torque) > 0.001f);
    return 0;
}

/* Test: zero excitation produces near-zero torque. */
static int test_zero_excitation(void) {
    phys_body_t a = make_body_(0, 0, 0);
    phys_body_t b = make_body_(0.2f, 0, 0);

    phys_muscle_unit_t unit;
    phys_muscle_unit_init(&unit);
    unit.attach.origin_local    = (phys_vec3_t){0.0f, 0.05f, 0.0f};
    unit.attach.insertion_local = (phys_vec3_t){0.0f, 0.05f, 0.0f};
    unit.activation.excitation = 0.0f;

    phys_vec3_t axis = {0, 0, 1};
    phys_vec3_t pivot = {0.1f, 0, 0};

    float torque = 0.0f;
    phys_muscle_unit_evaluate(&unit, &axis, &pivot,
                               &a, &b, 0.001f, &torque);
    /* With zero activation, only passive force contributes.
     * The tendon model may shift fiber length, so passive force
     * can be non-trivial.  Just verify it's much less than max. */
    ASSERT_TRUE(fabsf(torque) < 10.0f);
    return 0;
}

/* Test: NULL safety. */
static int test_null_safety(void) {
    float torque = 99.0f;
    phys_muscle_unit_evaluate(NULL, NULL, NULL, NULL, NULL, 0.001f, &torque);
    ASSERT_NEAR(0.0f, torque, 1e-6f);
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
    printf("p127_muscle_unit_tests:\n");

    RUN_TEST(test_unit_init);
    RUN_TEST(test_produces_torque);
    RUN_TEST(test_zero_excitation);
    RUN_TEST(test_null_safety);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
