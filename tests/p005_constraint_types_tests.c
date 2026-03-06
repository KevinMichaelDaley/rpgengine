/**
 * @file p005_constraint_types_tests.c
 * @brief Unit tests for the unified constraint type system.
 *
 * Tests cover:
 * - Constraint type enum completeness and name lookup
 * - constraint_space_t enum values
 * - constraint_def_t initialization and tagged union access
 * - skeleton_def_t allocation, population, and destruction
 * - Edge cases: invalid types, null pointers, zero-bone skeletons
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"

/* ── Minimal test harness ────────────────────────────────────────── */

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

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ASSERT_EQ failed: %s:%d: %s == %s (%d != %d)\n", \
               __FILE__, __LINE__, #a, #b, (int)(a), (int)(b)); \
        return 1; \
    } \
} while (0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  ASSERT_STREQ failed: %s:%d: \"%s\" != \"%s\"\n", \
               __FILE__, __LINE__, (a), (b)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_FLOAT_EQ failed: %s:%d: %f != %f\n", \
               __FILE__, __LINE__, (double)(a), (double)(b)); \
        return 1; \
    } \
} while (0)

#define ASSERT_NULL(p) do { \
    if ((p) != NULL) { \
        printf("  ASSERT_NULL failed: %s:%d: %s\n", __FILE__, __LINE__, #p); \
        return 1; \
    } \
} while (0)

#define ASSERT_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        printf("  ASSERT_NOT_NULL failed: %s:%d: %s\n", __FILE__, __LINE__, #p); \
        return 1; \
    } \
} while (0)

/* ── Test: all 20 constraint type enum values exist ──────────────── */

static int test_constraint_type_enum_values(void) {
    /* Verify all 20 types have distinct values. */
    constraint_type_t types[] = {
        CONSTRAINT_IK,
        CONSTRAINT_SPLINE_IK,
        CONSTRAINT_CHILD_OF,
        CONSTRAINT_COPY_TRANSFORMS,
        CONSTRAINT_COPY_ROTATION,
        CONSTRAINT_COPY_LOCATION,
        CONSTRAINT_COPY_SCALE,
        CONSTRAINT_DAMPED_TRACK,
        CONSTRAINT_TRACK_TO,
        CONSTRAINT_LOCKED_TRACK,
        CONSTRAINT_LIMIT_ROTATION,
        CONSTRAINT_LIMIT_LOCATION,
        CONSTRAINT_LIMIT_SCALE,
        CONSTRAINT_TRANSFORMATION,
        CONSTRAINT_ACTION,
        CONSTRAINT_CLAMP_TO,
        CONSTRAINT_FLOOR,
        CONSTRAINT_MAINTAIN_VOLUME,
        CONSTRAINT_SHRINKWRAP,
        CONSTRAINT_PIVOT,
    };
    int count = (int)(sizeof(types) / sizeof(types[0]));
    ASSERT_EQ(count, 20);

    /* Check all values are unique. */
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            ASSERT_TRUE(types[i] != types[j]);
        }
    }

    /* CONSTRAINT_TYPE_COUNT should equal 20. */
    ASSERT_EQ(CONSTRAINT_TYPE_COUNT, 20);

    return 0;
}

/* ── Test: constraint_type_name round-trip ───────────────────────── */

static int test_constraint_type_name(void) {
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_IK), "IK");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_SPLINE_IK), "Spline IK");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_CHILD_OF), "Child Of");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_COPY_TRANSFORMS), "Copy Transforms");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_COPY_ROTATION), "Copy Rotation");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_COPY_LOCATION), "Copy Location");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_COPY_SCALE), "Copy Scale");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_DAMPED_TRACK), "Damped Track");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_TRACK_TO), "Track To");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_LOCKED_TRACK), "Locked Track");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_LIMIT_ROTATION), "Limit Rotation");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_LIMIT_LOCATION), "Limit Location");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_LIMIT_SCALE), "Limit Scale");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_TRANSFORMATION), "Transformation");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_ACTION), "Action");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_CLAMP_TO), "Clamp To");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_FLOOR), "Floor");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_MAINTAIN_VOLUME), "Maintain Volume");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_SHRINKWRAP), "Shrinkwrap");
    ASSERT_STREQ(constraint_type_name(CONSTRAINT_PIVOT), "Pivot");
    return 0;
}

/* ── Test: invalid type name returns "Unknown" ──────────────────── */

static int test_constraint_type_name_invalid(void) {
    ASSERT_STREQ(constraint_type_name((constraint_type_t)999), "Unknown");
    ASSERT_STREQ(constraint_type_name((constraint_type_t)-1), "Unknown");
    return 0;
}

/* ── Test: constraint_type_is_valid ──────────────────────────────── */

static int test_constraint_type_is_valid(void) {
    ASSERT_TRUE(constraint_type_is_valid(CONSTRAINT_IK));
    ASSERT_TRUE(constraint_type_is_valid(CONSTRAINT_PIVOT));
    ASSERT_TRUE(constraint_type_is_valid(CONSTRAINT_FLOOR));
    ASSERT_TRUE(!constraint_type_is_valid((constraint_type_t)999));
    ASSERT_TRUE(!constraint_type_is_valid((constraint_type_t)-1));
    return 0;
}

/* ── Test: constraint_space_t values ────────────────────────────── */

static int test_constraint_space_enum(void) {
    /* All 4 spaces must exist and be distinct. */
    constraint_space_t spaces[] = {
        CONSTRAINT_SPACE_WORLD,
        CONSTRAINT_SPACE_LOCAL,
        CONSTRAINT_SPACE_POSE,
        CONSTRAINT_SPACE_BONE,
    };
    for (int i = 0; i < 4; i++) {
        for (int j = i + 1; j < 4; j++) {
            ASSERT_TRUE(spaces[i] != spaces[j]);
        }
    }
    return 0;
}

/* ── Test: constraint_def_t initialization (IK) ─────────────────── */

static int test_constraint_def_ik(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_IK;
    def.influence = 1.0f;
    def.owner_space = CONSTRAINT_SPACE_POSE;
    def.target_space = CONSTRAINT_SPACE_WORLD;
    def.target_bone_idx = 5;
    def.params.ik.chain_length = 3;
    def.params.ik.iterations = 10;
    def.params.ik.weight = 1.0f;
    def.params.ik.pole_target_idx = 12;
    def.params.ik.use_tail = true;
    def.params.ik.orient_weight = 0.5f;

    ASSERT_EQ(def.type, CONSTRAINT_IK);
    ASSERT_FLOAT_EQ(def.influence, 1.0f, 1e-6f);
    ASSERT_EQ(def.params.ik.chain_length, 3);
    ASSERT_EQ(def.params.ik.iterations, 10);
    ASSERT_FLOAT_EQ(def.params.ik.weight, 1.0f, 1e-6f);
    ASSERT_EQ(def.params.ik.pole_target_idx, 12);
    ASSERT_TRUE(def.params.ik.use_tail);
    ASSERT_FLOAT_EQ(def.params.ik.orient_weight, 0.5f, 1e-6f);
    return 0;
}

/* ── Test: constraint_def_t initialization (Limit Rotation) ─────── */

static int test_constraint_def_limit_rotation(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_LIMIT_ROTATION;
    def.influence = 0.8f;
    def.owner_space = CONSTRAINT_SPACE_LOCAL;
    def.target_space = CONSTRAINT_SPACE_WORLD;
    def.target_bone_idx = UINT32_MAX; /* no target */

    def.params.limit_rotation.min_x = -1.57f;
    def.params.limit_rotation.max_x =  1.57f;
    def.params.limit_rotation.min_y = -0.5f;
    def.params.limit_rotation.max_y =  0.5f;
    def.params.limit_rotation.min_z =  0.0f;
    def.params.limit_rotation.max_z =  0.0f;
    def.params.limit_rotation.use_limit_x = true;
    def.params.limit_rotation.use_limit_y = true;
    def.params.limit_rotation.use_limit_z = false;

    ASSERT_EQ(def.type, CONSTRAINT_LIMIT_ROTATION);
    ASSERT_FLOAT_EQ(def.influence, 0.8f, 1e-6f);
    ASSERT_FLOAT_EQ(def.params.limit_rotation.min_x, -1.57f, 1e-6f);
    ASSERT_FLOAT_EQ(def.params.limit_rotation.max_x,  1.57f, 1e-6f);
    ASSERT_TRUE(def.params.limit_rotation.use_limit_x);
    ASSERT_TRUE(def.params.limit_rotation.use_limit_y);
    ASSERT_TRUE(!def.params.limit_rotation.use_limit_z);
    return 0;
}

/* ── Test: constraint_def_t initialization (Damped Track) ────────── */

static int test_constraint_def_damped_track(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_DAMPED_TRACK;
    def.influence = 1.0f;
    def.target_bone_idx = 7;
    def.params.damped_track.track_axis = CONSTRAINT_AXIS_NEG_Y;

    ASSERT_EQ(def.type, CONSTRAINT_DAMPED_TRACK);
    ASSERT_EQ(def.params.damped_track.track_axis, CONSTRAINT_AXIS_NEG_Y);
    return 0;
}

/* ── Test: constraint_def_t initialization (Copy Transforms) ────── */

static int test_constraint_def_copy_transforms(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_COPY_TRANSFORMS;
    def.influence = 0.5f;
    def.target_bone_idx = 3;
    def.params.copy_transforms.mix_mode = CONSTRAINT_MIX_REPLACE;

    ASSERT_EQ(def.type, CONSTRAINT_COPY_TRANSFORMS);
    ASSERT_FLOAT_EQ(def.influence, 0.5f, 1e-6f);
    ASSERT_EQ(def.params.copy_transforms.mix_mode, CONSTRAINT_MIX_REPLACE);
    return 0;
}

/* ── Test: constraint_def_t initialization (Floor) ───────────────── */

static int test_constraint_def_floor(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_FLOOR;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.floor.offset = 0.05f;
    def.params.floor.use_rotation = true;
    def.params.floor.floor_location = CONSTRAINT_FLOOR_BELOW_NEG_Y;

    ASSERT_EQ(def.type, CONSTRAINT_FLOOR);
    ASSERT_FLOAT_EQ(def.params.floor.offset, 0.05f, 1e-6f);
    ASSERT_TRUE(def.params.floor.use_rotation);
    ASSERT_EQ(def.params.floor.floor_location, CONSTRAINT_FLOOR_BELOW_NEG_Y);
    return 0;
}

/* ── Test: skeleton_def_t create and destroy ─────────────────────── */

static int test_skeleton_def_create_destroy(void) {
    skeleton_def_t skel;
    bool ok = skeleton_def_init(&skel, 4, 8);
    ASSERT_TRUE(ok);

    ASSERT_EQ(skel.joint_count, 4);
    ASSERT_EQ(skel.max_constraints_per_joint, 8);
    ASSERT_NOT_NULL(skel.joint_names);
    ASSERT_NOT_NULL(skel.parent_indices);
    ASSERT_NOT_NULL(skel.rest_local);
    ASSERT_NOT_NULL(skel.rest_world);
    ASSERT_NOT_NULL(skel.constraint_counts);
    ASSERT_NOT_NULL(skel.constraints);

    /* Verify initial state: all parent indices should be UINT32_MAX (root). */
    for (uint32_t i = 0; i < 4; i++) {
        ASSERT_EQ(skel.parent_indices[i], UINT32_MAX);
        ASSERT_EQ(skel.constraint_counts[i], 0);
    }

    skeleton_def_destroy(&skel);
    ASSERT_NULL(skel.joint_names);
    ASSERT_NULL(skel.parent_indices);
    ASSERT_NULL(skel.rest_local);
    ASSERT_NULL(skel.rest_world);
    return 0;
}

/* ── Test: skeleton_def_t populate and query ──────────────────────── */

static int test_skeleton_def_populate(void) {
    skeleton_def_t skel;
    bool ok = skeleton_def_init(&skel, 3, 4);
    ASSERT_TRUE(ok);

    /* Set up a simple 3-bone chain: root -> child -> grandchild. */
    strncpy(skel.joint_names[0], "root", SKELETON_JOINT_NAME_MAX - 1);
    strncpy(skel.joint_names[1], "child", SKELETON_JOINT_NAME_MAX - 1);
    strncpy(skel.joint_names[2], "grandchild", SKELETON_JOINT_NAME_MAX - 1);

    skel.parent_indices[0] = UINT32_MAX; /* root */
    skel.parent_indices[1] = 0;
    skel.parent_indices[2] = 1;

    /* Add a limit rotation constraint to joint 1. */
    constraint_def_t limit;
    memset(&limit, 0, sizeof(limit));
    limit.type = CONSTRAINT_LIMIT_ROTATION;
    limit.influence = 1.0f;
    limit.owner_space = CONSTRAINT_SPACE_LOCAL;
    limit.params.limit_rotation.min_x = -1.0f;
    limit.params.limit_rotation.max_x =  1.0f;
    limit.params.limit_rotation.use_limit_x = true;

    skel.constraints[1 * skel.max_constraints_per_joint + 0] = limit;
    skel.constraint_counts[1] = 1;

    /* Verify. */
    ASSERT_STREQ(skel.joint_names[0], "root");
    ASSERT_STREQ(skel.joint_names[2], "grandchild");
    ASSERT_EQ(skel.parent_indices[1], 0);
    ASSERT_EQ(skel.constraint_counts[1], 1);
    ASSERT_EQ(skel.constraints[1 * skel.max_constraints_per_joint + 0].type,
              CONSTRAINT_LIMIT_ROTATION);

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: skeleton_def_t with zero joints ───────────────────────── */

static int test_skeleton_def_zero_joints(void) {
    skeleton_def_t skel;
    /* Zero joints should fail. */
    bool ok = skeleton_def_init(&skel, 0, 4);
    ASSERT_TRUE(!ok);
    return 0;
}

/* ── Test: skeleton_def_destroy on zeroed struct is safe ─────────── */

static int test_skeleton_def_destroy_null(void) {
    skeleton_def_t skel;
    memset(&skel, 0, sizeof(skel));
    /* Should not crash. */
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: constraint_def_t with all tracking types ──────────────── */

static int test_constraint_def_tracking_types(void) {
    /* Track To */
    constraint_def_t track_to;
    memset(&track_to, 0, sizeof(track_to));
    track_to.type = CONSTRAINT_TRACK_TO;
    track_to.params.track_to.track_axis = CONSTRAINT_AXIS_Y;
    track_to.params.track_to.up_axis = CONSTRAINT_AXIS_Z;
    ASSERT_EQ(track_to.params.track_to.track_axis, CONSTRAINT_AXIS_Y);
    ASSERT_EQ(track_to.params.track_to.up_axis, CONSTRAINT_AXIS_Z);

    /* Locked Track */
    constraint_def_t locked;
    memset(&locked, 0, sizeof(locked));
    locked.type = CONSTRAINT_LOCKED_TRACK;
    locked.params.locked_track.track_axis = CONSTRAINT_AXIS_NEG_Z;
    locked.params.locked_track.lock_axis = CONSTRAINT_AXIS_Y;
    ASSERT_EQ(locked.params.locked_track.track_axis, CONSTRAINT_AXIS_NEG_Z);
    ASSERT_EQ(locked.params.locked_track.lock_axis, CONSTRAINT_AXIS_Y);

    return 0;
}

/* ── Test: constraint_def_t with copy location (axis masking) ────── */

static int test_constraint_def_copy_location(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_COPY_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = 2;
    def.params.copy_location.use_x = true;
    def.params.copy_location.use_y = false;
    def.params.copy_location.use_z = true;
    def.params.copy_location.invert_x = false;
    def.params.copy_location.invert_y = false;
    def.params.copy_location.invert_z = true;
    def.params.copy_location.offset = true;

    ASSERT_TRUE(def.params.copy_location.use_x);
    ASSERT_TRUE(!def.params.copy_location.use_y);
    ASSERT_TRUE(def.params.copy_location.use_z);
    ASSERT_TRUE(def.params.copy_location.invert_z);
    ASSERT_TRUE(def.params.copy_location.offset);
    return 0;
}

/* ── Test: constraint_def_t with transformation mapping ──────────── */

static int test_constraint_def_transformation(void) {
    constraint_def_t def;
    memset(&def, 0, sizeof(def));

    def.type = CONSTRAINT_TRANSFORMATION;
    def.influence = 1.0f;
    def.params.transformation.from_channel = CONSTRAINT_CHANNEL_LOC_X;
    def.params.transformation.to_channel = CONSTRAINT_CHANNEL_ROT_Z;
    def.params.transformation.from_min = 0.0f;
    def.params.transformation.from_max = 1.0f;
    def.params.transformation.to_min = 0.0f;
    def.params.transformation.to_max = 3.14159f;
    def.params.transformation.extrapolate = false;

    ASSERT_EQ(def.params.transformation.from_channel, CONSTRAINT_CHANNEL_LOC_X);
    ASSERT_EQ(def.params.transformation.to_channel, CONSTRAINT_CHANNEL_ROT_Z);
    ASSERT_FLOAT_EQ(def.params.transformation.to_max, 3.14159f, 1e-4f);
    return 0;
}

/* ── Test: axis enum completeness ────────────────────────────────── */

static int test_constraint_axis_enum(void) {
    /* 6 axis values: +X, +Y, +Z, -X, -Y, -Z */
    ASSERT_TRUE(CONSTRAINT_AXIS_X != CONSTRAINT_AXIS_Y);
    ASSERT_TRUE(CONSTRAINT_AXIS_Y != CONSTRAINT_AXIS_Z);
    ASSERT_TRUE(CONSTRAINT_AXIS_NEG_X != CONSTRAINT_AXIS_X);
    ASSERT_TRUE(CONSTRAINT_AXIS_NEG_Y != CONSTRAINT_AXIS_Y);
    ASSERT_TRUE(CONSTRAINT_AXIS_NEG_Z != CONSTRAINT_AXIS_Z);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_constraint_type_enum_values);
    RUN(test_constraint_type_name);
    RUN(test_constraint_type_name_invalid);
    RUN(test_constraint_type_is_valid);
    RUN(test_constraint_space_enum);
    RUN(test_constraint_def_ik);
    RUN(test_constraint_def_limit_rotation);
    RUN(test_constraint_def_damped_track);
    RUN(test_constraint_def_copy_transforms);
    RUN(test_constraint_def_floor);
    RUN(test_skeleton_def_create_destroy);
    RUN(test_skeleton_def_populate);
    RUN(test_skeleton_def_zero_joints);
    RUN(test_skeleton_def_destroy_null);
    RUN(test_constraint_def_tracking_types);
    RUN(test_constraint_def_copy_location);
    RUN(test_constraint_def_transformation);
    RUN(test_constraint_axis_enum);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
