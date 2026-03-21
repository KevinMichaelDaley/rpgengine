/**
 * @file bone_gizmo_tests.c
 * @brief Tests for per-bone gizmo build and transform application.
 */

#include "ferrum/editor/scene/scene_gizmo_bone.h"
#include "ferrum/editor/edit_bone_selection.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define FLOAT_EQ(a, b) (fabsf((a) - (b)) < 1e-4f)

/* ---- Helpers ---- */

/** Create a minimal skeleton with N joints at given positions. */
static void make_test_skeleton(skeleton_def_t *skel, uint32_t count,
                                float heads[][3],
                                float tails[][3]) {
    memset(skel, 0, sizeof(*skel));
    skel->joint_count = count;

    /* Allocate arrays. */
    skel->rest_world = calloc(count, sizeof(mat4_t));
    skel->rest_local = calloc(count, sizeof(mat4_t));
    skel->tail_positions = calloc(count * 3, sizeof(float));
    skel->parent_indices = calloc(count, sizeof(uint32_t));

    for (uint32_t i = 0; i < count; i++) {
        /* Set identity matrix with translation. */
        skel->rest_world[i] = mat4_identity();
        skel->rest_world[i].m[12] = heads[i][0];
        skel->rest_world[i].m[13] = heads[i][1];
        skel->rest_world[i].m[14] = heads[i][2];

        /* rest_local encodes offset from parent.
         * For root: rest_local = rest_world.
         * For child: rest_local = inverse(parent_world) * child_world
         *          = identity with offset (child_head - parent_head). */
        skel->rest_local[i] = mat4_identity();
        if (i == 0) {
            skel->rest_local[i].m[12] = heads[i][0];
            skel->rest_local[i].m[13] = heads[i][1];
            skel->rest_local[i].m[14] = heads[i][2];
        } else {
            uint32_t pi = 0; /* parent is always 0 in test skeletons */
            skel->rest_local[i].m[12] = heads[i][0] - heads[pi][0];
            skel->rest_local[i].m[13] = heads[i][1] - heads[pi][1];
            skel->rest_local[i].m[14] = heads[i][2] - heads[pi][2];
        }

        skel->tail_positions[i * 3 + 0] = tails[i][0];
        skel->tail_positions[i * 3 + 1] = tails[i][1];
        skel->tail_positions[i * 3 + 2] = tails[i][2];

        skel->parent_indices[i] = (i == 0) ? UINT32_MAX : 0;
    }
}

static void free_test_skeleton(skeleton_def_t *skel) {
    free(skel->rest_world);
    free(skel->rest_local);
    free(skel->tail_positions);
    free(skel->parent_indices);
    memset(skel, 0, sizeof(*skel));
}

/* ---- Build tests ---- */

static void test_build_3_selected(void) {
    float heads[][3] = {{0,0,0}, {1,0,0}, {2,0,0}, {3,0,0}};
    float tails[][3] = {{0,1,0}, {1,1,0}, {2,1,0}, {3,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 4, heads, tails);

    edit_bone_selection_t bone_sel;
    edit_bone_selection_init(&bone_sel);
    edit_bone_selection_add(&bone_sel, 10, 0);
    edit_bone_selection_add(&bone_sel, 10, 2);
    edit_bone_selection_add(&bone_sel, 10, 3);

    per_bone_gizmo_t gizmos[8];
    mat4_t entity_model;
    entity_model = mat4_identity();

    uint32_t count = per_bone_gizmo_build(
        &skel, &bone_sel, &entity_model, GIZMO_TRANSLATE, gizmos, 8);
    ASSERT(count == 3);

    /* Verify gizmo positions correspond to bone head positions. */
    bool found[4] = {false};
    for (uint32_t i = 0; i < count; i++) {
        found[gizmos[i].bone_index] = true;
        /* Gizmo position should be at the bone's head world position. */
        float expected_x = heads[gizmos[i].bone_index][0];
        ASSERT(FLOAT_EQ(gizmos[i].gizmo.position.x, expected_x));
    }
    ASSERT(found[0] && found[2] && found[3]);
    ASSERT(!found[1]); /* bone 1 not selected */

    free_test_skeleton(&skel);
}

static void test_build_empty_selection(void) {
    float heads[][3] = {{0,0,0}};
    float tails[][3] = {{0,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 1, heads, tails);

    edit_bone_selection_t bone_sel;
    edit_bone_selection_init(&bone_sel);

    per_bone_gizmo_t gizmos[4];
    mat4_t entity_model;
    entity_model = mat4_identity();

    uint32_t count = per_bone_gizmo_build(
        &skel, &bone_sel, &entity_model, GIZMO_TRANSLATE, gizmos, 4);
    ASSERT(count == 0);

    free_test_skeleton(&skel);
}

static void test_build_capacity_truncates(void) {
    float heads[][3] = {{0,0,0}, {1,0,0}, {2,0,0}};
    float tails[][3] = {{0,1,0}, {1,1,0}, {2,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 3, heads, tails);

    edit_bone_selection_t bone_sel;
    edit_bone_selection_init(&bone_sel);
    edit_bone_selection_add(&bone_sel, 10, 0);
    edit_bone_selection_add(&bone_sel, 10, 1);
    edit_bone_selection_add(&bone_sel, 10, 2);

    per_bone_gizmo_t gizmos[2]; /* capacity < selection */
    mat4_t entity_model;
    entity_model = mat4_identity();

    uint32_t count = per_bone_gizmo_build(
        &skel, &bone_sel, &entity_model, GIZMO_TRANSLATE, gizmos, 2);
    ASSERT(count == 2);

    free_test_skeleton(&skel);
}

static void test_build_null_returns_zero(void) {
    uint32_t count = per_bone_gizmo_build(NULL, NULL, NULL, 0, NULL, 0);
    ASSERT(count == 0);
}

static void test_build_with_entity_offset(void) {
    float heads[][3] = {{0,0,0}};
    float tails[][3] = {{0,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 1, heads, tails);

    edit_bone_selection_t bone_sel;
    edit_bone_selection_init(&bone_sel);
    edit_bone_selection_add(&bone_sel, 10, 0);

    per_bone_gizmo_t gizmos[4];
    mat4_t entity_model;
    entity_model = mat4_identity();
    entity_model.m[12] = 100.0f; /* offset X */
    entity_model.m[13] = 200.0f; /* offset Y */

    uint32_t count = per_bone_gizmo_build(
        &skel, &bone_sel, &entity_model, GIZMO_TRANSLATE, gizmos, 4);
    ASSERT(count == 1);
    /* Gizmo position should be bone head + entity offset. */
    ASSERT(FLOAT_EQ(gizmos[0].gizmo.position.x, 100.0f));
    ASSERT(FLOAT_EQ(gizmos[0].gizmo.position.y, 200.0f));

    free_test_skeleton(&skel);
}

/* ---- Apply drag tests ---- */

static void test_apply_drag_moves_bone(void) {
    float heads[][3] = {{0,0,0}, {1,0,0}};
    float tails[][3] = {{0,1,0}, {1,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 2, heads, tails);

    vec3_t delta = {5.0f, 3.0f, -1.0f};
    per_bone_gizmo_apply_drag(&skel, 1, delta);

    /* Bone 1 head should have moved. */
    ASSERT(FLOAT_EQ(skel.rest_world[1].m[12], 6.0f));
    ASSERT(FLOAT_EQ(skel.rest_world[1].m[13], 3.0f));
    ASSERT(FLOAT_EQ(skel.rest_world[1].m[14], -1.0f));

    /* Tail should have moved by the same delta. */
    ASSERT(FLOAT_EQ(skel.tail_positions[1 * 3 + 0], 6.0f));
    ASSERT(FLOAT_EQ(skel.tail_positions[1 * 3 + 1], 4.0f));
    ASSERT(FLOAT_EQ(skel.tail_positions[1 * 3 + 2], -1.0f));

    /* Bone 0 should be unchanged. */
    ASSERT(FLOAT_EQ(skel.rest_world[0].m[12], 0.0f));

    free_test_skeleton(&skel);
}

static void test_apply_drag_out_of_range(void) {
    float heads[][3] = {{0,0,0}};
    float tails[][3] = {{0,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 1, heads, tails);

    vec3_t delta = {1.0f, 1.0f, 1.0f};
    per_bone_gizmo_apply_drag(&skel, 99, delta); /* out of range */

    /* Nothing should have changed. */
    ASSERT(FLOAT_EQ(skel.rest_world[0].m[12], 0.0f));

    free_test_skeleton(&skel);
}

static void test_apply_drag_null_safe(void) {
    vec3_t delta = {1.0f, 0, 0};
    per_bone_gizmo_apply_drag(NULL, 0, delta);
    ASSERT(1);
}

/* ---- Apply rotate tests ---- */

static void test_apply_rotate_modifies_matrix(void) {
    float heads[][3] = {{0,0,0}};
    float tails[][3] = {{0,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 1, heads, tails);

    /* 90 degree rotation around Z axis. */
    quat_t dq = {0, 0, 0.7071068f, 0.7071068f}; /* sin(45), cos(45) */
    per_bone_gizmo_apply_rotate(&skel, 0, dq);

    /* After 90° Z rotation, the Y-up bone should now point in -X. */
    /* The matrix columns should have rotated. */
    /* Column 0 (X axis) was (1,0,0), should now be (0,1,0). */
    ASSERT(FLOAT_EQ(skel.rest_world[0].m[0], 0.0f));
    ASSERT(FLOAT_EQ(skel.rest_world[0].m[1], 1.0f));

    free_test_skeleton(&skel);
}

static void test_apply_rotate_null_safe(void) {
    quat_t dq = {0, 0, 0, 1};
    per_bone_gizmo_apply_rotate(NULL, 0, dq);
    ASSERT(1);
}

static void test_apply_rotate_out_of_range(void) {
    float heads[][3] = {{0,0,0}};
    float tails[][3] = {{0,1,0}};

    skeleton_def_t skel;
    make_test_skeleton(&skel, 1, heads, tails);

    quat_t dq = {0, 0, 0, 1}; /* identity */
    per_bone_gizmo_apply_rotate(&skel, 99, dq);

    /* Nothing should change. */
    ASSERT(FLOAT_EQ(skel.rest_world[0].m[0], 1.0f));

    free_test_skeleton(&skel);
}

int main(void) {
    printf("bone_gizmo_tests:\n");
    test_build_3_selected();
    test_build_empty_selection();
    test_build_capacity_truncates();
    test_build_null_returns_zero();
    test_build_with_entity_offset();
    test_apply_drag_moves_bone();
    test_apply_drag_out_of_range();
    test_apply_drag_null_safe();
    test_apply_rotate_modifies_matrix();
    test_apply_rotate_null_safe();
    test_apply_rotate_out_of_range();
    printf("bone_gizmo_tests: %d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
