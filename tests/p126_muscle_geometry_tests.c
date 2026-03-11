/**
 * @file p126_muscle_geometry_tests.c
 * @brief Tests for muscle attachment geometry and moment arm.
 */

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/muscle/geometry.h"
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

/** Create an identity-oriented body at given position. */
static phys_body_t make_body_(float x, float y, float z) {
    phys_body_t b;
    memset(&b, 0, sizeof(b));
    b.position = (phys_vec3_t){x, y, z};
    b.orientation = (phys_quat_t){0, 0, 0, 1}; /* identity */
    b.inv_mass = 1.0f;
    return b;
}

/* Test: init zeroes attachment. */
static int test_attach_init(void) {
    phys_muscle_attach_t att;
    att.origin_local = (phys_vec3_t){99, 99, 99};
    phys_muscle_attach_init(&att);
    ASSERT_NEAR(0.0f, att.origin_local.x, 1e-6f);
    ASSERT_NEAR(0.0f, att.insertion_local.x, 1e-6f);
    return 0;
}

/* Test: straight-line moment arm for simple geometry.
 * Body A at origin, body B at (1,0,0).
 * Origin at (0, 0.1, 0) on A, insertion at (0, 0.1, 0) on B.
 * Joint axis = Z, pivot at (0.5, 0, 0).
 * Muscle runs parallel to X at y=0.1. Moment arm ~= 0.1. */
static int test_straight_line_moment_arm(void) {
    phys_body_t a = make_body_(0, 0, 0);
    phys_body_t b = make_body_(1, 0, 0);

    phys_muscle_attach_t att;
    phys_muscle_attach_init(&att);
    att.origin_local    = (phys_vec3_t){0.0f, 0.1f, 0.0f};
    att.insertion_local = (phys_vec3_t){0.0f, 0.1f, 0.0f};

    phys_vec3_t axis = {0, 0, 1};
    phys_vec3_t pivot = {0.5f, 0, 0};

    float moment_arm = 0.0f;
    float fiber_len  = 0.0f;
    phys_muscle_geometry_moment_arm(&att, NULL, &axis, &pivot,
                                     &a, &b, &moment_arm, &fiber_len);

    /* Fiber length should be ~1.0 (distance between world points). */
    ASSERT_NEAR(1.0f, fiber_len, 0.01f);
    /* Moment arm should be ~0.1 (perpendicular distance). */
    ASSERT_NEAR(0.1f, fabsf(moment_arm), 0.02f);
    return 0;
}

/* Test: fiber length changes with body separation. */
static int test_fiber_length_varies(void) {
    phys_body_t a = make_body_(0, 0, 0);
    phys_body_t b1 = make_body_(1, 0, 0);
    phys_body_t b2 = make_body_(2, 0, 0);

    phys_muscle_attach_t att;
    phys_muscle_attach_init(&att);

    phys_vec3_t axis = {0, 0, 1};
    phys_vec3_t pivot = {0, 0, 0};

    float len1 = 0, len2 = 0, ma = 0;
    phys_muscle_geometry_moment_arm(&att, NULL, &axis, &pivot,
                                     &a, &b1, &ma, &len1);
    phys_muscle_geometry_moment_arm(&att, NULL, &axis, &pivot,
                                     &a, &b2, &ma, &len2);
    ASSERT_TRUE(len2 > len1);
    return 0;
}

/* Test: NULL pointers handled safely. */
static int test_null_safety(void) {
    float ma = 99.0f;
    phys_muscle_geometry_moment_arm(NULL, NULL, NULL, NULL,
                                     NULL, NULL, &ma, NULL);
    ASSERT_NEAR(0.0f, ma, 1e-6f);
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
    printf("p126_muscle_geometry_tests:\n");

    RUN_TEST(test_attach_init);
    RUN_TEST(test_straight_line_moment_arm);
    RUN_TEST(test_fiber_length_varies);
    RUN_TEST(test_null_safety);

    printf("%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
