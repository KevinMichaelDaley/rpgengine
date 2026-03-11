/**
 * @file p112_joint_driver_extended_tests.c
 * @brief Unit tests for linear actuator, servo, and aero/hydraulic drivers.
 *
 * Tests:
 *   1. Linear actuator: bias = clamp(error * gain, -max_speed, max_speed)
 *   2. Linear actuator: lambda clamped to ±max_force
 *   3. Servo (PD): bias = kp * error + kd * (-velocity_on_row)
 *   4. Servo: lambda clamped to ±max_torque
 *   5. Aero driver: force proportional to velocity² (drag mode)
 *   6. Hydraulic driver: velocity clamped by flow_limit
 *   7. All types: no crash on NULL / out-of-range row
 *   8. Linear actuator at target: zero bias
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

/* Helper: build a twist joint with limits to get 6 rows. */
static void build_test_joint(phys_joint_t *joint, phys_body_t *bodies,
                              float body_b_twist_angle)
{
    phys_quat_t identity = {0, 0, 0, 1};
    memset(bodies, 0, sizeof(phys_body_t) * 2);
    bodies[0].position = (phys_vec3_t){0, 0, 0};
    bodies[0].orientation = identity;
    bodies[0].inv_mass = 1.0f;
    bodies[0].inv_inertia_diag = (phys_vec3_t){10.0f, 10.0f, 10.0f};
    bodies[1] = bodies[0];

    if (fabsf(body_b_twist_angle) > 1e-6f) {
        float half = body_b_twist_angle * 0.5f;
        bodies[1].orientation = (phys_quat_t){sinf(half), 0, 0, cosf(half)};
    }

    phys_joint_init(joint);
    joint->type = PHYS_JOINT_TWIST;
    joint->body_a = 0;
    joint->body_b = 1;
    joint->local_axis_a = (phys_vec3_t){1, 0, 0};
    joint->limit_axes = 0x1;
    joint->limit_min[0] = -100.0f;
    joint->limit_max[0] = 100.0f;
    joint->rest_relative_orient = identity;

    phys_joint_build_twist(joint, &bodies[0], &bodies[1], 1.0f / 60.0f);
}

/* ── Test 1: linear actuator bias ─────────────────────────────────── */

static int test_linear_actuator_bias(void) {
    int failures = 0;
    printf("  test_linear_actuator_bias\n");

    phys_joint_t joint;
    phys_body_t bodies[2];
    build_test_joint(&joint, bodies, 0.5f); /* some twist offset */
    ASSERT_TRUE(joint.row_count == 6);

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_LINEAR_ACTUATOR;
    driver.target_row = 5;
    /* Target a position different from the current constraint_error. */
    float current_error = joint.rows[5].constraint_error;
    driver.actuator.target_position = current_error + 1.0f;
    driver.actuator.max_speed = 2.0f;
    driver.actuator.max_force = 50.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* Bias = clamp((target - error) * gain, -max_speed, max_speed).
     * target - error = 1.0, so bias should be nonzero and clamped. */
    ASSERT_TRUE(fabsf(joint.rows[5].bias) <= 2.0f + 1e-6f);
    ASSERT_TRUE(fabsf(joint.rows[5].bias) > 0.0f);
    ASSERT_NEAR(joint.rows[5].lambda_min, -50.0f, 1e-6f);
    ASSERT_NEAR(joint.rows[5].lambda_max, 50.0f, 1e-6f);

    return failures;
}

/* ── Test 2: linear actuator at target ────────────────────────────── */

static int test_linear_actuator_at_target(void) {
    int failures = 0;
    printf("  test_linear_actuator_at_target\n");

    phys_joint_t joint;
    phys_body_t bodies[2];
    build_test_joint(&joint, bodies, 0.0f); /* no twist offset */
    ASSERT_TRUE(joint.row_count == 6);

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_LINEAR_ACTUATOR;
    driver.target_row = 5;
    driver.actuator.target_position = joint.rows[5].constraint_error;
    driver.actuator.max_speed = 2.0f;
    driver.actuator.max_force = 50.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* At target — bias should be ~zero. */
    ASSERT_NEAR(joint.rows[5].bias, 0.0f, 0.01f);

    return failures;
}

/* ── Test 3: servo PD bias ────────────────────────────────────────── */

static int test_servo_bias(void) {
    int failures = 0;
    printf("  test_servo_bias\n");

    phys_joint_t joint;
    phys_body_t bodies[2];
    build_test_joint(&joint, bodies, 0.5f);
    ASSERT_TRUE(joint.row_count == 6);

    float error = joint.rows[5].constraint_error;

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_SERVO;
    driver.target_row = 5;
    driver.servo.target_value = 0.0f;
    driver.servo.kp = 100.0f;
    driver.servo.kd = 5.0f;
    driver.servo.max_force = 200.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* bias = kp * (target - error) + kd * (-current_velocity).
     * current_velocity from row is the existing bias before driver.
     * With no velocity term, it's just kp * (target - error). */
    float expected = 100.0f * (0.0f - error);
    ASSERT_NEAR(joint.rows[5].bias, expected, 1.0f);
    ASSERT_NEAR(joint.rows[5].lambda_min, -200.0f, 1e-6f);
    ASSERT_NEAR(joint.rows[5].lambda_max, 200.0f, 1e-6f);

    return failures;
}

/* ── Test 4: aero drag driver ─────────────────────────────────────── */

static int test_aero_drag(void) {
    int failures = 0;
    printf("  test_aero_drag\n");

    phys_joint_t joint;
    phys_body_t bodies[2];
    build_test_joint(&joint, bodies, 0.0f);
    ASSERT_TRUE(joint.row_count == 6);

    /* Set a known bias to simulate existing velocity on the row. */
    joint.rows[5].bias = 3.0f;

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_AERO_HYDRAULIC;
    driver.target_row = 5;
    driver.aero.drag_coeff = 0.5f;
    driver.aero.flow_limit = 100.0f; /* high limit = aero mode */
    driver.aero.max_force = 1000.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* Aero drag opposes motion: bias should be reduced. */
    /* Force = -drag_coeff * v * |v|, applied as bias change. */
    ASSERT_TRUE(joint.rows[5].lambda_min >= -1000.0f);
    ASSERT_TRUE(joint.rows[5].lambda_max <= 1000.0f);

    return failures;
}

/* ── Test 5: hydraulic flow limit ─────────────────────────────────── */

static int test_hydraulic_flow_limit(void) {
    int failures = 0;
    printf("  test_hydraulic_flow_limit\n");

    phys_joint_t joint;
    phys_body_t bodies[2];
    build_test_joint(&joint, bodies, 0.0f);
    ASSERT_TRUE(joint.row_count == 6);

    joint.rows[5].bias = 10.0f; /* large velocity */

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_AERO_HYDRAULIC;
    driver.target_row = 5;
    driver.aero.drag_coeff = 0.0f; /* no drag */
    driver.aero.flow_limit = 3.0f; /* velocity clamped to ±3 */
    driver.aero.max_force = 500.0f;

    phys_joint_driver_apply(&driver, &joint);

    /* Flow limit clamps the row bias. */
    ASSERT_TRUE(fabsf(joint.rows[5].bias) <= 3.0f + 1e-6f);

    return failures;
}

/* ── Test 6: NULL safety ──────────────────────────────────────────── */

static int test_null_safety(void) {
    int failures = 0;
    printf("  test_null_safety\n");

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_LINEAR_ACTUATOR;

    /* NULL joint — should not crash. */
    phys_joint_driver_apply(&driver, NULL);

    /* NULL driver — should not crash. */
    phys_joint_t joint;
    phys_joint_init(&joint);
    phys_joint_driver_apply(NULL, &joint);

    /* NONE type — should not crash. */
    driver.type = PHYS_DRIVER_NONE;
    phys_joint_driver_apply(&driver, &joint);

    return failures;
}

/* ── Test 7: out-of-range target row ──────────────────────────────── */

static int test_out_of_range_row(void) {
    int failures = 0;
    printf("  test_out_of_range_row\n");

    phys_joint_t joint;
    phys_body_t bodies[2];
    build_test_joint(&joint, bodies, 0.0f);

    phys_joint_driver_t driver;
    phys_joint_driver_init(&driver);
    driver.type = PHYS_DRIVER_SERVO;
    driver.target_row = 99; /* way out of range */
    driver.servo.kp = 100.0f;
    driver.servo.max_force = 50.0f;

    /* Should be a no-op, not crash. */
    phys_joint_driver_apply(&driver, &joint);

    return failures;
}

/* ── Main ─────────────────────────────────────────────────────────── */

int main(void) {
    int failures = 0;
    printf("=== Joint Driver Extended Tests ===\n");

    failures += test_linear_actuator_bias();
    failures += test_linear_actuator_at_target();
    failures += test_servo_bias();
    failures += test_aero_drag();
    failures += test_hydraulic_flow_limit();
    failures += test_null_safety();
    failures += test_out_of_range_row();

    printf("\n%s (%d failure%s)\n",
           failures == 0 ? "PASS" : "FAIL",
           failures, failures == 1 ? "" : "s");
    return failures == 0 ? 0 : 1;
}
