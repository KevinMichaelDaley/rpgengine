/**
 * @file p111_joint_driver_tests.c
 * @brief Unit tests for joint driver architecture and base types.
 *
 * Tests:
 *   1. Driver init: default state is NONE
 *   2. Set motor driver: type and params stored
 *   3. Clear driver: resets to NONE
 *   4. Apply motor driver: modifies row bias to target velocity
 *   5. Apply spring driver: modifies row bias with restoring force
 *   6. NULL/invalid inputs: graceful no-op
 *   7. Driver on joint with no rows: no crash
 */

#include "ferrum/physics/joint_driver.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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

/* ── Test 1: driver init ──────────────────────────────────────── */

static int test_driver_init(void) {
    int failures = 0;
    printf("  test_driver_init\n");

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);

    ASSERT_TRUE(driver.type == PHYS_DRIVER_NONE);
    ASSERT_TRUE(driver.target_row == 0);

    return failures;
}

/* ── Test 2: set motor driver ─────────────────────────────────── */

static int test_set_motor(void) {
    int failures = 0;
    printf("  test_set_motor\n");

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);

    driver.type = PHYS_DRIVER_MOTOR;
    driver.target_row = 3; /* drive angular row 3 */
    driver.motor.target_velocity = 10.0f;
    driver.motor.max_force = 100.0f;

    ASSERT_TRUE(driver.type == PHYS_DRIVER_MOTOR);
    ASSERT_NEAR(driver.motor.target_velocity, 10.0f, 1e-6f);
    ASSERT_NEAR(driver.motor.max_force, 100.0f, 1e-6f);
    ASSERT_TRUE(driver.target_row == 3);

    return failures;
}

/* ── Test 3: clear driver ─────────────────────────────────────── */

static int test_clear_driver(void) {
    int failures = 0;
    printf("  test_clear_driver\n");

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_MOTOR;
    driver.motor.target_velocity = 10.0f;

    phys_joint_driver_init(&driver); /* clear */

    ASSERT_TRUE(driver.type == PHYS_DRIVER_NONE);

    return failures;
}

/* ── Test 4: apply motor driver ───────────────────────────────── */

static int test_apply_motor(void) {
    int failures = 0;
    printf("  test_apply_motor\n");

    /* Build a twist joint so we have angular rows. */
    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_axis_a = (phys_vec3_t){1, 0, 0};

    phys_quat_t identity = {0, 0, 0, 1};
    phys_body_t bodies[2];
    memset(bodies, 0, sizeof(bodies));
    bodies[0].position = (phys_vec3_t){0, 0, 0};
    bodies[0].orientation = identity;
    bodies[0].inv_mass = 1.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){10.0f, 10.0f, 10.0f};
    bodies[1] = bodies[0];

    phys_joint_build_twist(&joint, &bodies[0], &bodies[1], 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 5);

    /* Set up motor driver on the twist axis.
     * For a twist joint without limits, there's no twist row — the
     * motor should add its own target.  But conceptually the driver
     * applies to a specific row index.  For a twist joint with limits
     * (row 5), target_row=5.  For one without, we enable limits to
     * get row 5. */
    joint.limit_axes = 0x1;
    joint.limit_min[0] = -100.0f; /* wide open */
    joint.limit_max[0] = 100.0f;
    joint.rest_relative_orient = identity;
    phys_joint_build_twist(&joint, &bodies[0], &bodies[1], 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 6);

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_MOTOR;
    driver.target_row = 5; /* twist limit row */
    driver.motor.target_velocity = 5.0f;
    driver.motor.max_force = 50.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* Motor sets bias to target_velocity on the target row. */
    ASSERT_NEAR(joint.rows[5].bias, 5.0f, 1e-6f);
    /* Motor clamps lambda to ±max_force. */
    ASSERT_NEAR(joint.rows[5].lambda_min, -50.0f, 1e-6f);
    ASSERT_NEAR(joint.rows[5].lambda_max, 50.0f, 1e-6f);

    return failures;
}

/* ── Test 5: apply spring driver ──────────────────────────────── */

static int test_apply_spring(void) {
    int failures = 0;
    printf("  test_apply_spring\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_TWIST;
    joint.body_a = 0;
    joint.body_b = 1;
    joint.local_axis_a = (phys_vec3_t){1, 0, 0};
    joint.limit_axes = 0x1;
    joint.limit_min[0] = -100.0f;
    joint.limit_max[0] = 100.0f;
    joint.rest_relative_orient = (phys_quat_t){0, 0, 0, 1};

    phys_quat_t identity = {0, 0, 0, 1};
    phys_body_t bodies[2];
    memset(bodies, 0, sizeof(bodies));
    bodies[0].position = (phys_vec3_t){0, 0, 0};
    bodies[0].orientation = identity;
    bodies[0].inv_mass = 1.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){10.0f, 10.0f, 10.0f};
    bodies[1] = bodies[0];

    /* Rotate B 30° about X to create angular error. */
    float half = 0.2618f; /* ~15° */
    bodies[1].orientation = (phys_quat_t){sinf(half), 0, 0, cosf(half)};

    phys_joint_build_twist(&joint, &bodies[0], &bodies[1], 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 6);

    /* Row 5 should have nonzero constraint_error from the angular offset. */
    float original_error = joint.rows[5].constraint_error;

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_SPRING;
    driver.target_row = 5;
    driver.spring.stiffness = 100.0f;
    driver.spring.damping = 5.0f;
    driver.spring.rest_value = 0.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* Spring sets bias = stiffness * (rest - current_error).
     * The sign convention: constraint_error measures deviation,
     * spring restores toward rest_value. */
    float expected_bias = driver.spring.stiffness *
                          (driver.spring.rest_value - original_error);
    ASSERT_NEAR(joint.rows[5].bias, expected_bias, 0.1f);
    /* Spring uses bilateral bounds. */
    ASSERT_TRUE(joint.rows[5].lambda_min < 0.0f);
    ASSERT_TRUE(joint.rows[5].lambda_max > 0.0f);

    return failures;
}

/* ── Test 6: NULL inputs ──────────────────────────────────────── */

static int test_null_inputs(void) {
    int failures = 0;
    printf("  test_null_inputs\n");

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_MOTOR;

    /* NULL joint — should not crash. */
    phys_joint_driver_apply(&driver, NULL);

    /* NULL driver — should not crash. */
    phys_joint_t joint;
    phys_joint_init(&joint);
    phys_joint_driver_apply(NULL, &joint);

    return failures;
}

/* ── Test 7: driver on joint with no rows ─────────────────────── */

static int test_no_rows(void) {
    int failures = 0;
    printf("  test_no_rows\n");

    phys_joint_t joint;
    phys_joint_init(&joint);
    /* row_count = 0 */

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_MOTOR;
    driver.target_row = 3;
    driver.motor.target_velocity = 10.0f;

    /* Should be a no-op, not crash. */
    phys_joint_driver_apply(&driver, &joint);

    return failures;
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    printf("=== Joint Driver Tests ===\n");

    failures += test_driver_init();
    failures += test_set_motor();
    failures += test_clear_driver();
    failures += test_apply_motor();
    failures += test_apply_spring();
    failures += test_null_inputs();
    failures += test_no_rows();

    printf("\n%s (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
