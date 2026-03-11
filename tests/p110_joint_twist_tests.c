/**
 * @file p110_joint_twist_tests.c
 * @brief Unit tests for PHYS_JOINT_TWIST (single-axis twist joint).
 *
 * Tests:
 *   1. Basic build: 5 rows (3 pos + 2 angular)
 *   2. Free twist: no error along twist axis
 *   3. Locked perp axes: error when rotated off twist axis
 *   4. Twist limits: 6th row appears, correct limit enforcement
 *   5. Warmstart: cached_lambda preserved
 *   6. NULL/invalid inputs: graceful no-op
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#define _USE_MATH_DEFINES
#include <math.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#define ASSERT_TRUE(cond) do {                                                  \
        if (!(cond)) {                                                          \
            printf("  FAIL: %s (line %d)\n", #cond, __LINE__);                  \
            failures++;                                                         \
        }                                                                       \
    } while (0)

#define ASSERT_NEAR(a, b, eps) do {                                             \
        float _a = (a), _b = (b);                                               \
        if (fabsf(_a - _b) > (eps)) {                                           \
            printf("  FAIL: %s ≈ %s  (%.6f vs %.6f, eps=%.6f) line %d\n",      \
                   #a, #b, _a, _b, (eps), __LINE__);                            \
            failures++;                                                         \
        }                                                                       \
    } while (0)

static phys_body_t make_body(phys_vec3_t pos, phys_quat_t orient, float mass) {
    phys_body_t b;
    memset(&b, 0, sizeof(b));
    b.position = pos;
    b.orientation = orient;
    b.inv_mass = (mass > 0.0f) ? (1.0f / mass) : 0.0f;
    /* Uniform sphere inertia for simplicity. */
    float I = (mass > 0.0f) ? (2.0f / 5.0f * mass * 0.5f * 0.5f) : 0.0f;
    float inv_I = (I > 0.0f) ? (1.0f / I) : 0.0f;
    b.inv_inertia_diag = (phys_vec3_t){inv_I, inv_I, inv_I};
    return b;
}

/* ── Test 1: basic build produces 5 rows ──────────────────────── */

static int test_basic_build(void) {
    int failures = 0;
    printf("  test_basic_build\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
    joint.local_axis_a = (phys_vec3_t){1, 0, 0}; /* twist around X */

    phys_quat_t identity = {0, 0, 0, 1};
    phys_body_t a = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);
    phys_body_t b = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);

    phys_joint_build_twist(&joint, &a, &b, 1.0f / 60.0f);

    ASSERT_TRUE(joint.row_count == 5);

    /* Rows 0-2 should not have ANGULAR flag. */
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE((joint.rows[i].flags & PHYS_ROW_FLAG_ANGULAR) == 0);
    }
    /* Rows 3-4 should have ANGULAR flag. */
    for (int i = 3; i < 5; i++) {
        ASSERT_TRUE((joint.rows[i].flags & PHYS_ROW_FLAG_ANGULAR) != 0);
    }

    return failures;
}

/* ── Test 2: free twist — no angular error when rotated about twist axis ── */

static int test_free_twist(void) {
    int failures = 0;
    printf("  test_free_twist\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_anchor_a = (phys_vec3_t){0.5f, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){-0.5f, 0, 0};
    joint.local_axis_a = (phys_vec3_t){1, 0, 0};

    phys_quat_t identity = {0, 0, 0, 1};
    /* Rotate body B by 90° about X (the twist axis). */
    float half = (float)M_PI / 4.0f; /* 45° = half of 90° */
    phys_quat_t rotX90 = {sinf(half), 0, 0, cosf(half)};

    phys_body_t a = make_body((phys_vec3_t){-0.5f, 0, 0}, identity, 1.0f);
    phys_body_t b = make_body((phys_vec3_t){0.5f, 0, 0}, rotX90, 1.0f);

    phys_joint_build_twist(&joint, &a, &b, 1.0f / 60.0f);

    ASSERT_TRUE(joint.row_count == 5);
    /* Angular rows should have near-zero bias (no error along perp axes
     * when rotation is purely about twist axis). */
    ASSERT_NEAR(joint.rows[3].bias, 0.0f, 0.01f);
    ASSERT_NEAR(joint.rows[4].bias, 0.0f, 0.01f);

    return failures;
}

/* ── Test 3: locked perp — error when rotated off twist axis ───── */

static int test_locked_perp(void) {
    int failures = 0;
    printf("  test_locked_perp\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
    joint.local_axis_a = (phys_vec3_t){1, 0, 0};

    phys_quat_t identity = {0, 0, 0, 1};
    /* Rotate body B by 30° about Y (perpendicular to twist axis). */
    float half = (float)(M_PI / 12.0); /* 15° = half of 30° */
    phys_quat_t rotY30 = {0, sinf(half), 0, cosf(half)};

    phys_body_t a = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);
    phys_body_t b = make_body((phys_vec3_t){0, 0, 0}, rotY30, 1.0f);

    phys_joint_build_twist(&joint, &a, &b, 1.0f / 60.0f);

    ASSERT_TRUE(joint.row_count == 5);
    /* At least one angular row should have nonzero bias. */
    float total_ang_error = fabsf(joint.rows[3].bias)
                          + fabsf(joint.rows[4].bias);
    ASSERT_TRUE(total_ang_error > 0.1f);

    return failures;
}

/* ── Test 4: twist limits — 6th row appears ───────────────────── */

static int test_twist_limits(void) {
    int failures = 0;
    printf("  test_twist_limits\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
    joint.local_axis_a = (phys_vec3_t){1, 0, 0};
    joint.rest_relative_orient = (phys_quat_t){0, 0, 0, 1};

    /* Enable twist limits: ±45° */
    joint.limit_axes = 0x1; /* bit 0 = twist axis */
    joint.limit_min[0] = -(float)M_PI / 4.0f;
    joint.limit_max[0] = (float)M_PI / 4.0f;

    phys_quat_t identity = {0, 0, 0, 1};
    /* Rotate body B by 60° about X — exceeds +45° limit. */
    float half = (float)(M_PI / 6.0); /* 30° = half of 60° */
    phys_quat_t rotX60 = {sinf(half), 0, 0, cosf(half)};

    phys_body_t a = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);
    phys_body_t b = make_body((phys_vec3_t){0, 0, 0}, rotX60, 1.0f);

    phys_joint_build_twist(&joint, &a, &b, 1.0f / 60.0f);

    /* Should have 6 rows: 3 pos + 2 ang lock + 1 twist limit. */
    ASSERT_TRUE(joint.row_count == 6);
    /* The limit row should have angular flag. */
    ASSERT_TRUE((joint.rows[5].flags & PHYS_ROW_FLAG_ANGULAR) != 0);
    /* Over the upper limit → negative lambda_max, pushing back. */
    ASSERT_TRUE(joint.rows[5].lambda_min < 0.0f);
    ASSERT_TRUE(joint.rows[5].bias != 0.0f);

    return failures;
}

/* ── Test 5: within limits — no limit row bias ────────────────── */

static int test_within_limits(void) {
    int failures = 0;
    printf("  test_within_limits\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
    joint.local_axis_a = (phys_vec3_t){1, 0, 0};
    joint.rest_relative_orient = (phys_quat_t){0, 0, 0, 1};

    joint.limit_axes = 0x1;
    joint.limit_min[0] = -(float)M_PI / 4.0f;
    joint.limit_max[0] = (float)M_PI / 4.0f;

    phys_quat_t identity = {0, 0, 0, 1};
    /* Rotate body B by 20° about X — within ±45° limits. */
    float half = (float)(M_PI / 18.0); /* 10° = half of 20° */
    phys_quat_t rotX20 = {sinf(half), 0, 0, cosf(half)};

    phys_body_t a = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);
    phys_body_t b = make_body((phys_vec3_t){0, 0, 0}, rotX20, 1.0f);

    phys_joint_build_twist(&joint, &a, &b, 1.0f / 60.0f);

    /* Still 6 rows (limit row active as bilateral speculative). */
    ASSERT_TRUE(joint.row_count == 6);
    /* Within limits — bias should be zero. */
    ASSERT_NEAR(joint.rows[5].bias, 0.0f, 0.001f);

    return failures;
}

/* ── Test 6: NULL/invalid inputs ──────────────────────────────── */

static int test_null_inputs(void) {
    int failures = 0;
    printf("  test_null_inputs\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;

    phys_quat_t identity = {0, 0, 0, 1};
    phys_body_t a = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);
    phys_body_t b = make_body((phys_vec3_t){0, 0, 0}, identity, 1.0f);

    /* NULL joint. */
    phys_joint_build_twist(NULL, &a, &b, 1.0f / 60.0f);

    /* NULL body_a. */
    phys_joint_build_twist(&joint, NULL, &b, 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 0);

    /* NULL body_b. */
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    phys_joint_build_twist(&joint, &a, NULL, 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 0);

    /* dt <= 0. */
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    phys_joint_build_twist(&joint, &a, &b, 0.0f);
    ASSERT_TRUE(joint.row_count == 0);

    return failures;
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    printf("=== Joint Twist Tests ===\n");

    failures += test_basic_build();
    failures += test_free_twist();
    failures += test_locked_perp();
    failures += test_twist_limits();
    failures += test_within_limits();
    failures += test_null_inputs();

    printf("\n%s (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
