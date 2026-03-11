/**
 * @file p128_muscle_pair_tests.c
 * @brief Tests for antagonist muscle pairing.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/muscle/muscle_pair.h"
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

/* Test: init sets defaults for both muscles. */
static int test_pair_init(void) {
    phys_muscle_pair_t pair;
    phys_muscle_pair_init(&pair);
    ASSERT_TRUE(pair.flexor.params.max_force > 0.0f);
    ASSERT_TRUE(pair.extensor.params.max_force > 0.0f);
    ASSERT_NEAR(0.0f, pair.flexor.activation.activation, 1e-6f);
    return 0;
}

/* Test: equal activation produces near-zero net torque (co-contraction). */
static int test_equal_activation_zero_net(void) {
    phys_body_t a = make_body_(0, 0, 0);
    phys_body_t b = make_body_(0.2f, 0, 0);

    phys_muscle_pair_t pair;
    phys_muscle_pair_init(&pair);

    /* Set identical attachment geometry for both muscles. */
    pair.flexor.attach.origin_local    = (phys_vec3_t){0, 0.05f, 0};
    pair.flexor.attach.insertion_local = (phys_vec3_t){0, 0.05f, 0};
    pair.extensor.attach.origin_local    = (phys_vec3_t){0, 0.05f, 0};
    pair.extensor.attach.insertion_local = (phys_vec3_t){0, 0.05f, 0};

    /* Same excitation for both. */
    pair.flexor.activation.excitation  = 1.0f;
    pair.extensor.activation.excitation = 1.0f;

    phys_vec3_t axis = {0, 0, 1};
    phys_vec3_t pivot = {0.1f, 0, 0};

    float net = 0, stiff = 0;
    for (int i = 0; i < 100; i++) {
        phys_muscle_pair_compute_torque(&pair, &axis, &pivot,
                                         &a, &b, 0.001f, &net, &stiff);
    }
    /* Net torque should be near zero (symmetric). */
    ASSERT_NEAR(0.0f, net, 0.5f);
    /* But stiffness should be positive (co-contraction). */
    ASSERT_TRUE(stiff > 0.0f);
    return 0;
}

/* Test: flexor-only produces positive net torque. */
static int test_flexor_only_positive(void) {
    phys_body_t a = make_body_(0, 0, 0);
    phys_body_t b = make_body_(0.2f, 0, 0);

    phys_muscle_pair_t pair;
    phys_muscle_pair_init(&pair);
    pair.flexor.attach.origin_local    = (phys_vec3_t){0, 0.05f, 0};
    pair.flexor.attach.insertion_local = (phys_vec3_t){0, 0.05f, 0};
    pair.extensor.attach.origin_local    = (phys_vec3_t){0, 0.05f, 0};
    pair.extensor.attach.insertion_local = (phys_vec3_t){0, 0.05f, 0};

    pair.flexor.activation.excitation = 1.0f;
    pair.extensor.activation.excitation = 0.0f;

    phys_vec3_t axis = {0, 0, 1};
    phys_vec3_t pivot = {0.1f, 0, 0};

    float net = 0, stiff = 0;
    for (int i = 0; i < 100; i++) {
        phys_muscle_pair_compute_torque(&pair, &axis, &pivot,
                                         &a, &b, 0.001f, &net, &stiff);
    }
    /* Flexor torque minus zero extensor = positive net. */
    ASSERT_TRUE(fabsf(net) > 0.001f);
    return 0;
}

/* Test: NULL safety. */
static int test_null_safety(void) {
    float net = 99.0f;
    phys_muscle_pair_compute_torque(NULL, NULL, NULL, NULL, NULL,
                                     0.001f, &net, NULL);
    ASSERT_NEAR(0.0f, net, 1e-6f);
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
    printf("p128_muscle_pair_tests:\n");

    RUN_TEST(test_pair_init);
    RUN_TEST(test_equal_activation_zero_net);
    RUN_TEST(test_flexor_only_positive);
    RUN_TEST(test_null_safety);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
