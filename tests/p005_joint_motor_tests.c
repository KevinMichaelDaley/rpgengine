/**
 * @file p005_joint_motor_tests.c
 * @brief Tests for physics joint motor system and ragdoll integration.
 *
 * Tests the motor target + strength system on joints and the ragdoll
 * struct that maps bones to physics bodies with the sequential
 * animation→physics pipeline.
 */

#define _USE_MATH_DEFINES
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ferrum/physics/joint.h"
#include "ferrum/physics/joint_motor.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/phys_types.h"
#include "ferrum/animation/ragdoll.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* ── Test harness ────────────────────────────────────────────────── */

static int g_pass, g_fail;

#define RUN(fn) do {                                    \
    printf("RUN  " #fn "\n");                           \
    int rc = fn();                                      \
    if (rc == 0) { printf("  OK " #fn "\n"); g_pass++; } \
    else { printf("FAIL " #fn "\n"); g_fail++; }        \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_FLOAT_EQ failed: %s:%d: %f != %f (diff=%f)\n", \
               __FILE__, __LINE__, (double)(a), (double)(b), \
               (double)fabsf((a) - (b))); \
        return 1; \
    } \
} while (0)

/* ── Helper: make a simple body ──────────────────────────────────── */

static phys_body_t make_body(float x, float y, float z, float mass) {
    phys_body_t b;
    phys_body_init(&b);
    b.position = (phys_vec3_t){x, y, z};
    b.orientation = (phys_quat_t){0.f, 0.f, 0.f, 1.f};
    if (mass > 0.f) {
        phys_body_set_mass(&b, mass);
        phys_body_set_box_inertia(&b, mass, (phys_vec3_t){0.5f, 0.5f, 0.5f});
    }
    return b;
}

/* ═══════════════════════════════════════════════════════════════════
 *  JOINT MOTOR TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/** Motor init sets strength to 0 and orientation to identity. */
static int test_motor_init(void) {
    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);

    ASSERT_FLOAT_EQ(motor.strength, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(motor.max_torque, 0.0f, 1e-6f);
    /* Identity quaternion. */
    ASSERT_FLOAT_EQ(motor.target_orientation.w, 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(motor.target_orientation.x, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(motor.target_orientation.y, 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(motor.target_orientation.z, 0.0f, 1e-6f);
    return 0;
}

/** Motor with strength=0 should not add any rows. */
static int test_motor_strength_zero_no_rows(void) {
    phys_body_t ba = make_body(0, 0, 0, 1.0f);
    phys_body_t bb = make_body(1, 0, 0, 1.0f);

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_BALL;
    joint.body_a = 0;
    joint.body_b = 1;
    phys_joint_build_ball(&joint, &ba, &bb, 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 3);

    /* Motor with strength=0. */
    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);
    motor.strength = 0.0f;
    /* target_orientation = 90° around Y */
    motor.target_orientation = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, (float)M_PI / 2.0f, 1e-6f);

    uint8_t added = phys_joint_motor_apply(&motor, &joint, &ba, &bb, 1.0f / 60.0f);
    /* Should not add rows when strength is 0. */
    ASSERT_TRUE(added == 0);
    ASSERT_TRUE(joint.row_count == 3);
    return 0;
}

/** Motor with strength=1 should add 3 angular rows. */
static int test_motor_strength_one_adds_rows(void) {
    phys_body_t ba = make_body(0, 0, 0, 1.0f);
    phys_body_t bb = make_body(1, 0, 0, 1.0f);

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_BALL;
    joint.body_a = 0;
    joint.body_b = 1;
    phys_joint_build_ball(&joint, &ba, &bb, 1.0f / 60.0f);
    ASSERT_TRUE(joint.row_count == 3);

    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);
    motor.strength = 1.0f;
    motor.max_torque = 1000.0f;
    /* Target: 90° rotation around Y axis. */
    motor.target_orientation = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, (float)M_PI / 2.0f, 1e-6f);

    uint8_t added = phys_joint_motor_apply(&motor, &joint, &ba, &bb, 1.0f / 60.0f);
    /* Should add 3 angular motor rows. */
    ASSERT_TRUE(added == 3);
    ASSERT_TRUE(joint.row_count == 6);

    /* Angular rows should have zero linear Jacobians. */
    for (int i = 3; i < 6; i++) {
        ASSERT_FLOAT_EQ(joint.rows[i].J_va.x, 0.0f, 1e-6f);
        ASSERT_FLOAT_EQ(joint.rows[i].J_va.y, 0.0f, 1e-6f);
        ASSERT_FLOAT_EQ(joint.rows[i].J_va.z, 0.0f, 1e-6f);
        ASSERT_FLOAT_EQ(joint.rows[i].J_vb.x, 0.0f, 1e-6f);
        ASSERT_FLOAT_EQ(joint.rows[i].J_vb.y, 0.0f, 1e-6f);
        ASSERT_FLOAT_EQ(joint.rows[i].J_vb.z, 0.0f, 1e-6f);
    }
    return 0;
}

/** Motor angular rows should have bias proportional to orientation error. */
static int test_motor_angular_bias(void) {
    phys_body_t ba = make_body(0, 0, 0, 1.0f);
    phys_body_t bb = make_body(1, 0, 0, 1.0f);
    /* Both bodies at identity orientation. */

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_BALL;
    joint.body_a = 0;
    joint.body_b = 1;
    phys_joint_build_ball(&joint, &ba, &bb, 1.0f / 60.0f);

    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);
    motor.strength = 1.0f;
    motor.max_torque = 1000.0f;
    /* Target: small rotation (0.1 rad) around Y.
     * Body B is at identity, so angular error = ~(0, 0.1, 0). */
    motor.target_orientation = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, 0.1f, 1e-6f);

    phys_joint_motor_apply(&motor, &joint, &ba, &bb, 1.0f / 60.0f);

    /* The Y-axis angular row should have non-zero bias. */
    float bias_y = joint.rows[4].bias;  /* row 4 = Y angular */
    /* Bias should reflect the angular error (~0.1 rad around Y). */
    ASSERT_TRUE(fabsf(bias_y) > 0.01f);

    /* X and Z angular rows should have near-zero bias (no error on those axes). */
    ASSERT_FLOAT_EQ(joint.rows[3].bias, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(joint.rows[5].bias, 0.0f, 0.01f);
    return 0;
}

/** Motor max_torque clamps lambda bounds. */
static int test_motor_max_torque_clamp(void) {
    phys_body_t ba = make_body(0, 0, 0, 1.0f);
    phys_body_t bb = make_body(1, 0, 0, 1.0f);

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_BALL;
    joint.body_a = 0;
    joint.body_b = 1;
    phys_joint_build_ball(&joint, &ba, &bb, 1.0f / 60.0f);

    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);
    motor.strength = 1.0f;
    motor.max_torque = 50.0f;
    motor.target_orientation = quat_from_axis_angle(
        (vec3_t){0, 1, 0}, 1.0f, 1e-6f);

    phys_joint_motor_apply(&motor, &joint, &ba, &bb, 1.0f / 60.0f);

    /* Angular row lambda bounds should be clamped to max_torque. */
    for (int i = 3; i < 6; i++) {
        ASSERT_FLOAT_EQ(joint.rows[i].lambda_min, -50.0f, 1e-6f);
        ASSERT_FLOAT_EQ(joint.rows[i].lambda_max,  50.0f, 1e-6f);
    }
    return 0;
}

/** When body B is already at the target orientation, motor bias should be ~0. */
static int test_motor_at_target_no_bias(void) {
    phys_body_t ba = make_body(0, 0, 0, 1.0f);
    phys_body_t bb = make_body(1, 0, 0, 1.0f);
    /* Set body B to 90° around Y. */
    bb.orientation = quat_from_axis_angle((vec3_t){0, 1, 0}, (float)M_PI / 2.0f, 1e-6f);

    phys_joint_t joint;
    phys_joint_init(&joint);
    joint.type = PHYS_JOINT_BALL;
    joint.body_a = 0;
    joint.body_b = 1;
    phys_joint_build_ball(&joint, &ba, &bb, 1.0f / 60.0f);

    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);
    motor.strength = 1.0f;
    motor.max_torque = 1000.0f;
    /* Target = same as body B's current orientation. */
    motor.target_orientation = bb.orientation;

    phys_joint_motor_apply(&motor, &joint, &ba, &bb, 1.0f / 60.0f);

    /* All angular biases should be near zero (already at target). */
    for (int i = 3; i < 6; i++) {
        ASSERT_FLOAT_EQ(joint.rows[i].bias, 0.0f, 0.01f);
    }
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  RAGDOLL TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/** Helper: build a 3-bone skeleton (root → mid → tip). */
static void make_3bone_skeleton(skeleton_def_t *skel) {
    memset(skel, 0, sizeof(*skel));
    skel->joint_count = 3;
    skel->max_constraints_per_joint = 0;

    /* Allocate arrays. */
    skel->parent_indices = (uint32_t *)calloc(3, sizeof(uint32_t));
    skel->rest_local = (mat4_t *)calloc(3, sizeof(mat4_t));
    skel->rest_world = (mat4_t *)calloc(3, sizeof(mat4_t));
    skel->joint_names = (char (*)[SKELETON_JOINT_NAME_MAX])calloc(3, SKELETON_JOINT_NAME_MAX);

    /* Root at origin. */
    skel->parent_indices[0] = UINT32_MAX;
    skel->rest_local[0] = mat4_identity();
    skel->rest_world[0] = mat4_identity();
    snprintf(skel->joint_names[0], SKELETON_JOINT_NAME_MAX, "root");

    /* Mid at y=2. */
    skel->parent_indices[1] = 0;
    skel->rest_local[1] = mat4_translation(0.f, 2.f, 0.f);
    skel->rest_world[1] = mat4_translation(0.f, 2.f, 0.f);
    snprintf(skel->joint_names[1], SKELETON_JOINT_NAME_MAX, "mid");

    /* Tip at y=4. */
    skel->parent_indices[2] = 1;
    skel->rest_local[2] = mat4_translation(0.f, 2.f, 0.f);
    skel->rest_world[2] = mat4_translation(0.f, 4.f, 0.f);
    snprintf(skel->joint_names[2], SKELETON_JOINT_NAME_MAX, "tip");
}

static void free_3bone_skeleton(skeleton_def_t *skel) {
    free(skel->parent_indices);
    free(skel->rest_local);
    free(skel->rest_world);
    free(skel->joint_names);
    memset(skel, 0, sizeof(*skel));
}

/** ragdoll_create produces correct bone_count and body/joint arrays. */
static int test_ragdoll_create(void) {
    skeleton_def_t skel;
    make_3bone_skeleton(&skel);

    ragdoll_t ragdoll;
    bool ok = ragdoll_create(&ragdoll, &skel, skel.rest_world);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(ragdoll.bone_count == 3);

    /* Every bone should have a valid body index. */
    for (uint32_t i = 0; i < ragdoll.bone_count; i++) {
        ASSERT_TRUE(ragdoll.body_indices[i] != UINT32_MAX);
    }

    /* Root has no joint (no parent). */
    ASSERT_TRUE(ragdoll.joint_indices[0] == UINT32_MAX);
    /* Non-root bones have joints. */
    ASSERT_TRUE(ragdoll.joint_indices[1] != UINT32_MAX);
    ASSERT_TRUE(ragdoll.joint_indices[2] != UINT32_MAX);

    /* Default motor strength = 1.0 (animation-dominated). */
    for (uint32_t i = 0; i < ragdoll.bone_count; i++) {
        ASSERT_FLOAT_EQ(ragdoll.motor_strengths[i], 1.0f, 1e-6f);
    }

    ragdoll_destroy(&ragdoll);
    free_3bone_skeleton(&skel);
    return 0;
}

/** Per-bone motor strength works. */
static int test_ragdoll_per_bone_motor(void) {
    skeleton_def_t skel;
    make_3bone_skeleton(&skel);

    ragdoll_t ragdoll;
    ragdoll_create(&ragdoll, &skel, skel.rest_world);

    /* Set all to 0.5. */
    ragdoll_set_motor_strength(&ragdoll, 0.5f);
    for (uint32_t i = 0; i < ragdoll.bone_count; i++) {
        ASSERT_FLOAT_EQ(ragdoll.motor_strengths[i], 0.5f, 1e-6f);
    }

    /* Override bone 1 to 0.0 (pure ragdoll for that bone). */
    ragdoll_set_bone_motor_strength(&ragdoll, 1, 0.0f);
    ASSERT_FLOAT_EQ(ragdoll.motor_strengths[0], 0.5f, 1e-6f);
    ASSERT_FLOAT_EQ(ragdoll.motor_strengths[1], 0.0f, 1e-6f);
    ASSERT_FLOAT_EQ(ragdoll.motor_strengths[2], 0.5f, 1e-6f);

    ragdoll_destroy(&ragdoll);
    free_3bone_skeleton(&skel);
    return 0;
}

/** update_motor_targets copies animation target orientations to motors. */
static int test_ragdoll_update_targets(void) {
    skeleton_def_t skel;
    make_3bone_skeleton(&skel);

    ragdoll_t ragdoll;
    ragdoll_create(&ragdoll, &skel, skel.rest_world);

    /* Create a target pose with bone 1 rotated 45° around Z. */
    mat4_t target_pose[3];
    target_pose[0] = mat4_identity();
    target_pose[1] = mat4_rotation_z(0.785f);  /* ~45 degrees */
    target_pose[1].m[12] = 0.f;
    target_pose[1].m[13] = 2.f;
    target_pose[1].m[14] = 0.f;
    target_pose[2] = mat4_translation(0.f, 4.f, 0.f);

    ragdoll_update_motor_targets(&ragdoll, target_pose, 3);

    /* Motor for bone 1 should have a non-identity target orientation. */
    ASSERT_TRUE(fabsf(ragdoll.motors[1].target_orientation.w - 1.0f) > 0.01f);

    /* Motor for bone 0 should have identity target (no rotation). */
    ASSERT_FLOAT_EQ(ragdoll.motors[0].target_orientation.w, 1.0f, 0.01f);

    ragdoll_destroy(&ragdoll);
    free_3bone_skeleton(&skel);
    return 0;
}

/** ragdoll_sync_from_physics copies body transforms into bone_world. */
static int test_ragdoll_sync_from_physics(void) {
    skeleton_def_t skel;
    make_3bone_skeleton(&skel);

    ragdoll_t ragdoll;
    ragdoll_create(&ragdoll, &skel, skel.rest_world);

    /* Manually modify the body positions stored in ragdoll. */
    ragdoll.bodies[0].position = (phys_vec3_t){10.f, 0.f, 0.f};
    ragdoll.bodies[1].position = (phys_vec3_t){10.f, 2.f, 0.f};
    ragdoll.bodies[2].position = (phys_vec3_t){10.f, 4.f, 0.f};

    ragdoll_sync_from_physics(&ragdoll);

    /* bone_world should reflect the body positions. */
    ASSERT_FLOAT_EQ(ragdoll.bone_world[0].m[12], 10.f, 1e-4f);
    ASSERT_FLOAT_EQ(ragdoll.bone_world[1].m[12], 10.f, 1e-4f);
    ASSERT_FLOAT_EQ(ragdoll.bone_world[2].m[12], 10.f, 1e-4f);

    ragdoll_destroy(&ragdoll);
    free_3bone_skeleton(&skel);
    return 0;
}

/** Null/edge case safety. */
static int test_motor_null_safety(void) {
    phys_joint_motor_init(NULL);  /* Should not crash. */

    phys_joint_motor_t motor;
    phys_joint_motor_init(&motor);
    motor.strength = 1.0f;

    /* NULL joint. */
    uint8_t added = phys_joint_motor_apply(&motor, NULL, NULL, NULL, 1.0f / 60.0f);
    ASSERT_TRUE(added == 0);

    /* NULL motor. */
    phys_joint_t joint;
    phys_joint_init(&joint);
    added = phys_joint_motor_apply(NULL, &joint, NULL, NULL, 1.0f / 60.0f);
    ASSERT_TRUE(added == 0);

    return 0;
}

/* ── main ────────────────────────────────────────────────────────── */

int main(void) {
    /* Motor tests. */
    RUN(test_motor_init);
    RUN(test_motor_strength_zero_no_rows);
    RUN(test_motor_strength_one_adds_rows);
    RUN(test_motor_angular_bias);
    RUN(test_motor_max_torque_clamp);
    RUN(test_motor_at_target_no_bias);
    RUN(test_motor_null_safety);

    /* Ragdoll tests. */
    RUN(test_ragdoll_create);
    RUN(test_ragdoll_per_bone_motor);
    RUN(test_ragdoll_update_targets);
    RUN(test_ragdoll_sync_from_physics);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
