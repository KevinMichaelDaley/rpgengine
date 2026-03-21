/**
 * @file prefab_pose_apply_tests.c
 * @brief Tests for prefab bone pose override application.
 */

#include "ferrum/editor/scene/prefab/prefab_pose_apply.h"
#include "ferrum/editor/scene/prefab/prefab_def.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_pass = 0;
static int s_fail = 0;

#define ASSERT(cond)                                              \
    do {                                                          \
        if (cond) { s_pass++; }                                   \
        else {                                                    \
            s_fail++;                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n",                  \
                    __FILE__, __LINE__, #cond);                   \
        }                                                        \
    } while (0)

#define ASSERT_NEAR(a, b, eps)                                    \
    do {                                                          \
        if (fabsf((a) - (b)) < (eps)) { s_pass++; }              \
        else {                                                    \
            s_fail++;                                             \
            fprintf(stderr, "FAIL %s:%d: %f != %f\n",            \
                    __FILE__, __LINE__, (double)(a), (double)(b));\
        }                                                        \
    } while (0)

/* ---- Test helpers ---- */

static void make_test_skeleton(skeleton_def_t *skel, uint32_t joint_count) {
    skeleton_def_init(skel, joint_count, 0);
    /* Set simple hierarchy: bone 0 = root, rest are children of 0. */
    skel->parent_indices[0] = UINT32_MAX;
    for (uint32_t i = 1; i < joint_count; i++) {
        skel->parent_indices[i] = 0;
    }
    /* Identity rest_local and rest_world. */
    for (uint32_t i = 0; i < joint_count; i++) {
        skel->rest_local[i] = mat4_identity();
        skel->rest_world[i] = mat4_identity();
    }
}

/* ---- Tests ---- */

static void test_null_args(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel, 2);
    prefab_def_t def;
    prefab_def_init(&def);

    ASSERT(!prefab_pose_apply(NULL, &skel));
    ASSERT(!prefab_pose_apply(&def, NULL));
    ASSERT(!prefab_pose_apply(NULL, NULL));

    skeleton_def_destroy(&skel);
}

static void test_zero_pose_count(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel, 3);
    prefab_def_t def;
    prefab_def_init(&def);
    def.bone_pose_count = 0;

    /* Should return false (nothing to apply). */
    ASSERT(!prefab_pose_apply(&def, &skel));

    /* Skeleton unchanged. */
    ASSERT_NEAR(skel.rest_local[0].m[0], 1.0f, 1e-6f);

    skeleton_def_destroy(&skel);
}

static void test_apply_translation_override(void) {
    skeleton_def_t skel;
    make_test_skeleton(&skel, 2);

    prefab_def_t def;
    prefab_def_init(&def);
    def.bone_pose_count = 2;

    /* Set bone 0 rest_local to identity with translation (5, 0, 0). */
    mat4_t m = mat4_identity();
    m.m[12] = 5.0f;
    memcpy(def.bone_rest_local[0], m.m, 16 * sizeof(float));

    /* Bone 1: identity with translation (0, 3, 0). */
    mat4_t m1 = mat4_identity();
    m1.m[13] = 3.0f;
    memcpy(def.bone_rest_local[1], m1.m, 16 * sizeof(float));

    ASSERT(prefab_pose_apply(&def, &skel));

    /* Check rest_local was overwritten. */
    ASSERT_NEAR(skel.rest_local[0].m[12], 5.0f, 1e-6f);
    ASSERT_NEAR(skel.rest_local[1].m[13], 3.0f, 1e-6f);

    /* Check rest_world was recomputed.
     * Bone 0 is root → rest_world = rest_local = translate(5,0,0).
     * Bone 1 parent=0 → rest_world = bone0_world * bone1_local
     *   = translate(5,0,0) * translate(0,3,0) = translate(5,3,0). */
    ASSERT_NEAR(skel.rest_world[0].m[12], 5.0f, 1e-6f);
    ASSERT_NEAR(skel.rest_world[0].m[13], 0.0f, 1e-6f);
    ASSERT_NEAR(skel.rest_world[1].m[12], 5.0f, 1e-6f);
    ASSERT_NEAR(skel.rest_world[1].m[13], 3.0f, 1e-6f);

    skeleton_def_destroy(&skel);
}

static void test_pose_count_exceeds_joints(void) {
    /* If prefab has more bone poses than skeleton joints, clamp. */
    skeleton_def_t skel;
    make_test_skeleton(&skel, 2);

    prefab_def_t def;
    prefab_def_init(&def);
    def.bone_pose_count = 10; /* More than 2 joints. */

    mat4_t m = mat4_identity();
    m.m[12] = 7.0f;
    memcpy(def.bone_rest_local[0], m.m, 16 * sizeof(float));
    memcpy(def.bone_rest_local[1], m.m, 16 * sizeof(float));

    /* Should still work, applying only 2 bone poses. */
    ASSERT(prefab_pose_apply(&def, &skel));
    ASSERT_NEAR(skel.rest_local[0].m[12], 7.0f, 1e-6f);
    ASSERT_NEAR(skel.rest_local[1].m[12], 7.0f, 1e-6f);

    skeleton_def_destroy(&skel);
}

static void test_roundtrip_save_load_apply(void) {
    /* Build a skeleton, modify bone 0, capture to prefab, reset, apply. */
    skeleton_def_t skel;
    make_test_skeleton(&skel, 3);

    /* Simulate bone gizmo edit: move bone 1 by (2, 0, 0). */
    skel.rest_local[1].m[12] = 2.0f;

    /* Capture into prefab_def. */
    prefab_def_t def;
    prefab_def_init(&def);
    def.bone_pose_count = 3;
    for (uint32_t i = 0; i < 3; i++) {
        memcpy(def.bone_rest_local[i], skel.rest_local[i].m,
               16 * sizeof(float));
    }

    /* Reset skeleton to identity (simulate reload from disk). */
    for (uint32_t i = 0; i < 3; i++) {
        skel.rest_local[i] = mat4_identity();
        skel.rest_world[i] = mat4_identity();
    }
    ASSERT_NEAR(skel.rest_local[1].m[12], 0.0f, 1e-6f);

    /* Apply poses from prefab. */
    ASSERT(prefab_pose_apply(&def, &skel));
    ASSERT_NEAR(skel.rest_local[1].m[12], 2.0f, 1e-6f);

    skeleton_def_destroy(&skel);
}

int main(void) {
    printf("prefab_pose_apply_tests:\n");

    test_null_args();
    test_zero_pose_count();
    test_apply_translation_override();
    test_pose_count_exceeds_joints();
    test_roundtrip_save_load_apply();

    printf("prefab_pose_apply_tests: %d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
