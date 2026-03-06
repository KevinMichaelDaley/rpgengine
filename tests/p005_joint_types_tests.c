/**
 * @file p005_joint_types_tests.c
 * @brief Unit tests for new physics joint types and animation adapters.
 *
 * Tests: lock, copy_rotation, limit_rotation, limit_position, aim joints.
 * Also tests bone_to_body adapter and animation constraint-to-joints mapping.
 */

#include "ferrum/physics/joint.h"
#include "ferrum/physics/body.h"
#include "ferrum/animation/bone_to_body.h"
#include "ferrum/animation/anim_constraint_rows.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/bone_collider.h"
#include "ferrum/animation/bone_joint_desc.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test harness macros ─────────────────────────────────────────── */

static int g_pass, g_fail;

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, eps) \
    ASSERT_TRUE(fabsf((a) - (b)) < (eps))

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn() == 0) { printf("  OK %s\n", #fn); g_pass++; } \
    else { g_fail++; } \
} while (0)

/* ── Helpers ─────────────────────────────────────────────────────── */

/** Create a body at position with identity orientation. */
static phys_body_t make_body(float x, float y, float z) {
    phys_body_t b;
    phys_body_init(&b);
    b.position = (phys_vec3_t){x, y, z};
    b.orientation = (phys_quat_t){0, 0, 0, 1};
    phys_body_set_mass(&b, 1.0f);
    phys_body_set_box_inertia(&b, 1.0f, (phys_vec3_t){0.1f, 0.1f, 0.1f});
    return b;
}

/** Create a body at position with a given orientation quaternion. */
static phys_body_t make_body_rot(float x, float y, float z, quat_t q) {
    phys_body_t b = make_body(x, y, z);
    b.orientation = q;
    return b;
}

/* ── Lock joint tests ────────────────────────────────────────────── */

static int test_lock_joint_produces_6_rows(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(1, 0, 0);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LOCK;
    j.body_a = 0;
    j.body_b = 1;

    phys_joint_build_lock(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 6);

    /* Rows 0-2: positional (has linear Jacobians). */
    for (int i = 0; i < 3; i++) {
        float lin_mag = fabsf(j.rows[i].J_vb.x) +
                        fabsf(j.rows[i].J_vb.y) +
                        fabsf(j.rows[i].J_vb.z);
        ASSERT_TRUE(lin_mag > 0.5f);
    }

    /* Rows 3-5: angular only (zero linear Jacobians). */
    for (int i = 3; i < 6; i++) {
        float lin_mag = fabsf(j.rows[i].J_vb.x) +
                        fabsf(j.rows[i].J_vb.y) +
                        fabsf(j.rows[i].J_vb.z);
        ASSERT_FLOAT_EQ(lin_mag, 0.0f, 1e-6f);
    }
    return 0;
}

static int test_lock_joint_zero_error_at_same_pose(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(0, 0, 0);  /* Same position/orientation. */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LOCK;
    j.body_a = 0;
    j.body_b = 1;

    phys_joint_build_lock(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 6);

    /* All biases should be near zero (no error). */
    for (int i = 0; i < 6; i++) {
        ASSERT_FLOAT_EQ(j.rows[i].bias, 0.0f, 1e-5f);
    }
    return 0;
}

/* ── Copy rotation joint tests ───────────────────────────────────── */

static int test_copy_rotation_3_angular_rows(void) {
    phys_body_t a = make_body(0, 0, 0);
    quat_t rot45 = quat_from_axis_angle((vec3_t){0, 1, 0}, 0.785f, 1e-8f);
    phys_body_t b = make_body_rot(2, 0, 0, rot45);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_COPY_ROTATION;
    j.body_a = 0;
    j.body_b = 1;

    phys_joint_build_copy_rotation(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 3);

    /* All rows should be angular only. */
    for (int i = 0; i < 3; i++) {
        float lin_mag = fabsf(j.rows[i].J_vb.x) +
                        fabsf(j.rows[i].J_vb.y) +
                        fabsf(j.rows[i].J_vb.z);
        ASSERT_FLOAT_EQ(lin_mag, 0.0f, 1e-6f);
    }

    /* Body b is rotated 45° around Y — bias on Y axis should be nonzero. */
    float total_bias = 0.0f;
    for (int i = 0; i < 3; i++) {
        total_bias += fabsf(j.rows[i].bias);
    }
    ASSERT_TRUE(total_bias > 0.1f);
    return 0;
}

static int test_copy_rotation_zero_at_matching(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(5, 3, 1);  /* Different position, same orient. */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_COPY_ROTATION;
    j.body_a = 0;
    j.body_b = 1;

    phys_joint_build_copy_rotation(&j, &a, &b, 1.0f / 60.0f);
    for (int i = 0; i < 3; i++) {
        ASSERT_FLOAT_EQ(j.rows[i].bias, 0.0f, 1e-5f);
    }
    return 0;
}

/* ── Limit rotation joint tests ──────────────────────────────────── */

static int test_limit_rotation_within_limits(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(0, 0, 0);  /* Same orientation. */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LIMIT_ROTATION;
    j.body_a = 0;
    j.body_b = 1;
    j.limit_axes = 0x7;  /* All three axes. */
    j.limit_min[0] = -1.0f; j.limit_max[0] = 1.0f;
    j.limit_min[1] = -1.0f; j.limit_max[1] = 1.0f;
    j.limit_min[2] = -1.0f; j.limit_max[2] = 1.0f;

    phys_joint_build_limit_rotation(&j, &a, &b, 1.0f / 60.0f);
    /* No violation — should produce 0 rows. */
    ASSERT_TRUE(j.row_count == 0);
    return 0;
}

static int test_limit_rotation_exceeds_max(void) {
    phys_body_t a = make_body(0, 0, 0);
    /* Rotate body b 90° around Y (1.57 rad > 1.0 limit). */
    quat_t rot90 = quat_from_axis_angle((vec3_t){0, 1, 0}, 1.57f, 1e-8f);
    phys_body_t b = make_body_rot(0, 0, 0, rot90);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LIMIT_ROTATION;
    j.body_a = 0;
    j.body_b = 1;
    j.limit_axes = 0x2;  /* Y axis only. */
    j.limit_min[1] = -1.0f;
    j.limit_max[1] = 1.0f;

    phys_joint_build_limit_rotation(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 1);
    /* Bias should be positive (exceeds max). */
    ASSERT_TRUE(j.rows[0].bias > 0.0f);
    /* One-sided: lambda_min = 0, lambda_max > 0. */
    ASSERT_FLOAT_EQ(j.rows[0].lambda_min, 0.0f, 1e-6f);
    ASSERT_TRUE(j.rows[0].lambda_max > 0.0f);
    return 0;
}

/* ── Limit position joint tests ──────────────────────────────────── */

static int test_limit_position_within_limits(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(0.5f, 0.5f, 0.5f);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LIMIT_POSITION;
    j.body_a = 0;
    j.body_b = 1;
    j.limit_axes = 0x7;
    j.limit_min[0] = -1.0f; j.limit_max[0] = 1.0f;
    j.limit_min[1] = -1.0f; j.limit_max[1] = 1.0f;
    j.limit_min[2] = -1.0f; j.limit_max[2] = 1.0f;

    phys_joint_build_limit_position(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 0);  /* Within limits. */
    return 0;
}

static int test_limit_position_exceeds_max(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(2.0f, 0, 0);  /* X=2, exceeds max=1. */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LIMIT_POSITION;
    j.body_a = 0;
    j.body_b = 1;
    j.limit_axes = 0x1;  /* X axis only. */
    j.limit_min[0] = -1.0f;
    j.limit_max[0] = 1.0f;

    phys_joint_build_limit_position(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 1);
    ASSERT_TRUE(j.rows[0].bias > 0.0f);  /* Exceeds max. */
    return 0;
}

/* ── Aim joint tests ─────────────────────────────────────────────── */

static int test_aim_produces_2_rows(void) {
    phys_body_t target = make_body(0, 5, 0);  /* Target above. */
    phys_body_t aimer  = make_body(0, 0, 0);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_AIM;
    j.body_a = 0;
    j.body_b = 1;
    j.track_axis = (phys_vec3_t){0, 1, 0};  /* +Y axis. */

    phys_joint_build_aim(&j, &target, &aimer, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 2);

    /* Both rows should be angular only. */
    for (int i = 0; i < 2; i++) {
        float lin = fabsf(j.rows[i].J_vb.x) +
                    fabsf(j.rows[i].J_vb.y) +
                    fabsf(j.rows[i].J_vb.z);
        ASSERT_FLOAT_EQ(lin, 0.0f, 1e-6f);
    }
    return 0;
}

static int test_aim_aligned_zero_error(void) {
    /* Target is directly along +Y from aimer, and aimer's +Y points up. */
    phys_body_t target = make_body(0, 5, 0);
    phys_body_t aimer  = make_body(0, 0, 0);  /* Identity orient, +Y is up. */

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_AIM;
    j.body_a = 0;
    j.body_b = 1;
    j.track_axis = (phys_vec3_t){0, 1, 0};

    phys_joint_build_aim(&j, &target, &aimer, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 2);

    /* Error should be near zero (already aligned). */
    float total_bias = fabsf(j.rows[0].bias) + fabsf(j.rows[1].bias);
    ASSERT_FLOAT_EQ(total_bias, 0.0f, 1e-4f);
    return 0;
}

/* ── Bone-to-body adapter tests ──────────────────────────────────── */

static int test_bones_to_bodies_position(void) {
    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    pose[0].m[12] = 1.0f; pose[0].m[13] = 2.0f; pose[0].m[14] = 3.0f;
    pose[1].m[12] = 4.0f; pose[1].m[13] = 5.0f; pose[1].m[14] = 6.0f;

    phys_body_t bodies[2];
    memset(bodies, 0, sizeof(bodies));
    phys_body_init(&bodies[0]);
    phys_body_init(&bodies[1]);

    anim_bones_to_bodies(pose, NULL, bodies, 2);

    ASSERT_FLOAT_EQ(bodies[0].position.x, 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(bodies[0].position.y, 2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(bodies[0].position.z, 3.0f, 1e-6f);
    ASSERT_FLOAT_EQ(bodies[1].position.x, 4.0f, 1e-6f);
    return 0;
}

static int test_bodies_to_bones_roundtrip(void) {
    /* Create a body at known pose, convert to bone, check. */
    phys_body_t b;
    phys_body_init(&b);
    b.position = (phys_vec3_t){3, 4, 5};
    b.orientation = (phys_quat_t){0, 0, 0, 1};

    mat4_t pose;
    anim_bodies_to_bones(&b, &pose, 1);

    ASSERT_FLOAT_EQ(pose.m[12], 3.0f, 1e-6f);
    ASSERT_FLOAT_EQ(pose.m[13], 4.0f, 1e-6f);
    ASSERT_FLOAT_EQ(pose.m[14], 5.0f, 1e-6f);

    /* Identity rotation → identity upper-left 3x3. */
    ASSERT_FLOAT_EQ(pose.m[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(pose.m[5], 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(pose.m[10], 1.0f, 1e-6f);
    return 0;
}

/* ── Constraint-to-joints adapter tests ──────────────────────────── */

static int test_copy_location_maps_to_ball(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    skel.max_constraints_per_joint = 1;

    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    uint32_t counts[2] = {0, 1};
    skel.constraint_counts = counts;

    constraint_def_t defs[2];
    memset(defs, 0, sizeof(defs));
    defs[1].type = CONSTRAINT_COPY_LOCATION;
    defs[1].influence = 1.0f;
    defs[1].target_bone_idx = 0;
    skel.constraints = defs;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    pose[1].m[12] = 1.0f;

    uint32_t body_map[2] = {0, 1};

    phys_joint_t out[4];
    uint32_t n = anim_constraints_to_joints(&skel, pose, body_map, out, 4);

    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_BALL);
    ASSERT_TRUE(out[0].body_a == 0);  /* Target. */
    ASSERT_TRUE(out[0].body_b == 1);  /* Owner. */
    return 0;
}

static int test_copy_rotation_maps_to_copy_rotation(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    skel.max_constraints_per_joint = 1;

    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;
    uint32_t counts[2] = {0, 1};
    skel.constraint_counts = counts;

    constraint_def_t defs[2];
    memset(defs, 0, sizeof(defs));
    defs[1].type = CONSTRAINT_COPY_ROTATION;
    defs[1].influence = 1.0f;
    defs[1].target_bone_idx = 0;
    skel.constraints = defs;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_constraints_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_COPY_ROTATION);
    return 0;
}

static int test_child_of_maps_to_lock(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    skel.max_constraints_per_joint = 1;

    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;
    uint32_t counts[2] = {0, 1};
    skel.constraint_counts = counts;

    constraint_def_t defs[2];
    memset(defs, 0, sizeof(defs));
    defs[1].type = CONSTRAINT_CHILD_OF;
    defs[1].influence = 1.0f;
    defs[1].target_bone_idx = 0;
    skel.constraints = defs;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_constraints_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_LOCK);
    return 0;
}

static int test_limit_rotation_maps_correctly(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    skel.max_constraints_per_joint = 1;

    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;
    uint32_t counts[2] = {0, 1};
    skel.constraint_counts = counts;

    constraint_def_t defs[2];
    memset(defs, 0, sizeof(defs));
    defs[1].type = CONSTRAINT_LIMIT_ROTATION;
    defs[1].influence = 1.0f;
    defs[1].target_bone_idx = UINT32_MAX;
    defs[1].params.limit_rotation.use_limit_x = true;
    defs[1].params.limit_rotation.min_x = -0.5f;
    defs[1].params.limit_rotation.max_x = 0.5f;
    skel.constraints = defs;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_constraints_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_LIMIT_ROTATION);
    ASSERT_TRUE(out[0].limit_axes == 1);
    ASSERT_FLOAT_EQ(out[0].limit_min[0], -0.5f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_max[0], 0.5f, 1e-6f);
    return 0;
}

/* ── Joint desc-to-joints adapter tests ──────────────────────────── */

static int test_joint_desc_ball_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[0].joint_type = 0;  /* Root: none. */
    jds[1].joint_type = 1;  /* Child: ball. */
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    pose[1].m[13] = 1.0f;  /* Child 1m above root. */

    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_BALL);
    ASSERT_TRUE(out[0].body_a == 0);
    ASSERT_TRUE(out[0].body_b == 1);
    /* Anchor A should be at child position relative to parent. */
    ASSERT_FLOAT_EQ(out[0].local_anchor_a.y, 1.0f, 1e-6f);
    return 0;
}

static int test_joint_desc_hinge_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[1].joint_type = 2;  /* Hinge. */
    jds[1].axis[0] = 1.0f;  /* X axis. */
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_HINGE);
    ASSERT_FLOAT_EQ(out[0].local_axis_a.x, 1.0f, 1e-6f);
    return 0;
}

/* ── Joint desc adapter tests for expanded types ────────────────── */

static int test_joint_desc_lock_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[1].joint_type = 4;  /* Lock. */
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_translation(1, 0, 0)};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_LOCK);
    return 0;
}

static int test_joint_desc_copy_rotation_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[1].joint_type = 5;  /* Copy rotation. */
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_COPY_ROTATION);
    return 0;
}

static int test_joint_desc_limit_rotation_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[1].joint_type = 6;  /* Limit rotation. */
    jds[1].limit_min[0] = -1.0f;
    jds[1].limit_max[0] = 1.0f;
    jds[1].limit_min[2] = -0.5f;
    jds[1].limit_max[2] = 0.5f;
    jds[1].limit_axes = 5;  /* X + Z. */
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_LIMIT_ROTATION);
    ASSERT_FLOAT_EQ(out[0].limit_min[0], -1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_max[0], 1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_min[2], -0.5f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_max[2], 0.5f, 1e-6f);
    ASSERT_TRUE(out[0].limit_axes == 5);
    return 0;
}

static int test_joint_desc_limit_position_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[1].joint_type = 7;  /* Limit position. */
    jds[1].limit_min[0] = -2.0f;
    jds[1].limit_max[0] = 2.0f;
    jds[1].limit_min[1] = -1.0f;
    jds[1].limit_max[1] = 3.0f;
    jds[1].limit_axes = 3;  /* X + Y. */
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_identity()};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_LIMIT_POSITION);
    ASSERT_FLOAT_EQ(out[0].limit_min[0], -2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_max[0], 2.0f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_min[1], -1.0f, 1e-6f);
    ASSERT_FLOAT_EQ(out[0].limit_max[1], 3.0f, 1e-6f);
    ASSERT_TRUE(out[0].limit_axes == 3);
    return 0;
}

static int test_joint_desc_aim_mapping(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    skel.joint_count = 2;
    uint32_t parents[2] = {UINT32_MAX, 0};
    skel.parent_indices = parents;

    bone_joint_desc_t jds[2];
    memset(jds, 0, sizeof(jds));
    jds[1].joint_type = 8;  /* Aim. */
    jds[1].axis[0] = 0.0f;
    jds[1].axis[1] = 1.0f;
    jds[1].axis[2] = 0.0f;
    skel.joints = jds;

    mat4_t pose[2] = {mat4_identity(), mat4_translation(1, 0, 0)};
    uint32_t body_map[2] = {0, 1};
    phys_joint_t out[4];

    uint32_t n = anim_joint_descs_to_joints(&skel, pose, body_map, out, 4);
    ASSERT_TRUE(n == 1);
    ASSERT_TRUE(out[0].type == PHYS_JOINT_AIM);
    ASSERT_FLOAT_EQ(out[0].track_axis.y, 1.0f, 1e-6f);
    return 0;
}

/* ── Null safety tests ───────────────────────────────────────────── */

static int test_null_safety(void) {
    /* All build functions should be no-ops with NULL. */
    phys_joint_t j;
    phys_joint_init(&j);

    phys_joint_build_lock(NULL, NULL, NULL, 0.016f);
    phys_joint_build_lock(&j, NULL, NULL, 0.016f);
    ASSERT_TRUE(j.row_count == 0);

    phys_joint_build_copy_rotation(NULL, NULL, NULL, 0.016f);
    phys_joint_build_copy_rotation(&j, NULL, NULL, 0.016f);
    ASSERT_TRUE(j.row_count == 0);

    phys_joint_build_limit_rotation(NULL, NULL, NULL, 0.016f);
    phys_joint_build_limit_rotation(&j, NULL, NULL, 0.016f);
    ASSERT_TRUE(j.row_count == 0);

    phys_joint_build_limit_position(NULL, NULL, NULL, 0.016f);
    phys_joint_build_limit_position(&j, NULL, NULL, 0.016f);
    ASSERT_TRUE(j.row_count == 0);

    phys_joint_build_aim(NULL, NULL, NULL, 0.016f);
    phys_joint_build_aim(&j, NULL, NULL, 0.016f);
    ASSERT_TRUE(j.row_count == 0);

    /* Adapter null safety. */
    ASSERT_TRUE(anim_constraints_to_joints(NULL, NULL, NULL, NULL, 0) == 0);
    ASSERT_TRUE(anim_joint_descs_to_joints(NULL, NULL, NULL, NULL, 0) == 0);

    anim_bones_to_bodies(NULL, NULL, NULL, 0);
    anim_bodies_to_bones(NULL, NULL, 0);

    return 0;
}

/* ── Constraint build → constraint conversion test ───────────────── */

static int test_lock_joint_to_constraints(void) {
    phys_body_t a = make_body(0, 0, 0);
    phys_body_t b = make_body(1, 0, 0);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_LOCK;
    j.body_a = 0;
    j.body_b = 1;

    phys_joint_build_lock(&j, &a, &b, 1.0f / 60.0f);
    ASSERT_TRUE(j.row_count == 6);

    /* Should produce 2 constraints (3 rows each). */
    phys_constraint_t out[3];
    uint32_t n = phys_joint_build_constraints(&j, out, 3, 1);
    ASSERT_TRUE(n == 2);
    ASSERT_TRUE(out[0].row_count == 3);
    ASSERT_TRUE(out[1].row_count == 3);
    ASSERT_TRUE(out[0].is_joint == 1);
    ASSERT_TRUE(out[1].is_joint == 1);
    return 0;
}

/* ── main ────────────────────────────────────────────────────────── */

int main(void) {
    /* Lock joint. */
    RUN(test_lock_joint_produces_6_rows);
    RUN(test_lock_joint_zero_error_at_same_pose);

    /* Copy rotation joint. */
    RUN(test_copy_rotation_3_angular_rows);
    RUN(test_copy_rotation_zero_at_matching);

    /* Limit rotation joint. */
    RUN(test_limit_rotation_within_limits);
    RUN(test_limit_rotation_exceeds_max);

    /* Limit position joint. */
    RUN(test_limit_position_within_limits);
    RUN(test_limit_position_exceeds_max);

    /* Aim joint. */
    RUN(test_aim_produces_2_rows);
    RUN(test_aim_aligned_zero_error);

    /* Bone-to-body adapter. */
    RUN(test_bones_to_bodies_position);
    RUN(test_bodies_to_bones_roundtrip);

    /* Constraint-to-joints adapter. */
    RUN(test_copy_location_maps_to_ball);
    RUN(test_copy_rotation_maps_to_copy_rotation);
    RUN(test_child_of_maps_to_lock);
    RUN(test_limit_rotation_maps_correctly);

    /* Joint desc adapter. */
    RUN(test_joint_desc_ball_mapping);
    RUN(test_joint_desc_hinge_mapping);
    RUN(test_joint_desc_lock_mapping);
    RUN(test_joint_desc_copy_rotation_mapping);
    RUN(test_joint_desc_limit_rotation_mapping);
    RUN(test_joint_desc_limit_position_mapping);
    RUN(test_joint_desc_aim_mapping);

    /* Null safety. */
    RUN(test_null_safety);

    /* Constraint conversion. */
    RUN(test_lock_joint_to_constraints);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
