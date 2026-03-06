/**
 * @file p005_transform_map_tests.c
 * @brief Tests for Transformation, Child Of, and Pivot constraints.
 *        Action constraint is stubbed (requires animation clip system).
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/transform_map.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

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

static vec3_t get_translation(const mat4_t *m) {
    return (vec3_t){ m->m[12], m->m[13], m->m[14] };
}

static void make_skel(skeleton_def_t *skel) {
    skeleton_def_init(skel, 2, 4);
    skel->parent_indices[0] = UINT32_MAX;
    skel->parent_indices[1] = 0;
    skel->rest_world[0] = mat4_identity();
    skel->rest_world[1] = mat4_identity();
}

/* ── Transformation constraint ───────────────────────────────────── */

static int test_transformation_location_to_location(void) {
    /* Target X location [0, 10] → owner Y location [0, 20].
     * Target at X=5, so owner Y = 10. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(5.0f, 0.0f, 0.0f); /* target */
    pose[1] = mat4_translation(0.0f, 0.0f, 0.0f); /* owner */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_TRANSFORMATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.transformation.from_channel = CONSTRAINT_CHANNEL_LOC_X;
    def.params.transformation.to_channel   = CONSTRAINT_CHANNEL_LOC_Y;
    def.params.transformation.from_min = 0.0f;
    def.params.transformation.from_max = 10.0f;
    def.params.transformation.to_min = 0.0f;
    def.params.transformation.to_max = 20.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_TRANSFORMATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.y, 10.0f, 0.1f); /* mapped: 5/10 * 20 = 10 */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_transformation_clamped(void) {
    /* Target value beyond from_max with extrapolate=false → clamped to to_max. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(20.0f, 0.0f, 0.0f); /* target: x=20 > from_max=10 */
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_TRANSFORMATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.transformation.from_channel = CONSTRAINT_CHANNEL_LOC_X;
    def.params.transformation.to_channel   = CONSTRAINT_CHANNEL_LOC_Y;
    def.params.transformation.from_min = 0.0f;
    def.params.transformation.from_max = 10.0f;
    def.params.transformation.to_min = 0.0f;
    def.params.transformation.to_max = 5.0f;
    def.params.transformation.extrapolate = false;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_TRANSFORMATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.y, 5.0f, 0.01f); /* clamped to to_max */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_transformation_extrapolate(void) {
    /* Target value beyond from_max with extrapolate=true → extends linearly. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(20.0f, 0.0f, 0.0f); /* x=20 */
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_TRANSFORMATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.transformation.from_channel = CONSTRAINT_CHANNEL_LOC_X;
    def.params.transformation.to_channel   = CONSTRAINT_CHANNEL_LOC_Y;
    def.params.transformation.from_min = 0.0f;
    def.params.transformation.from_max = 10.0f;
    def.params.transformation.to_min = 0.0f;
    def.params.transformation.to_max = 5.0f;
    def.params.transformation.extrapolate = true;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_TRANSFORMATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.y, 10.0f, 0.1f); /* extrapolated: 20/10 * 5 = 10 */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_transformation_zero_range(void) {
    /* from_min == from_max → degenerate, no crash. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(5.0f, 0.0f, 0.0f);
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_TRANSFORMATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.transformation.from_channel = CONSTRAINT_CHANNEL_LOC_X;
    def.params.transformation.to_channel   = CONSTRAINT_CHANNEL_LOC_Y;
    def.params.transformation.from_min = 5.0f;
    def.params.transformation.from_max = 5.0f; /* zero range */
    def.params.transformation.to_min = 0.0f;
    def.params.transformation.to_max = 10.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_TRANSFORMATION);
    fn(&def, &ctx, &pose[1]);

    /* No crash, no NaN. */
    vec3_t pos = get_translation(&pose[1]);
    ASSERT_TRUE(!isnan(pos.y));

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Child Of constraint ─────────────────────────────────────────── */

static int test_child_of_full(void) {
    /* Owner becomes child of target. inverse = identity.
     * Result: owner_world = target_world × identity × owner_local = target_world. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(10.0f, 0.0f, 0.0f); /* target/parent */
    pose[1] = mat4_identity(); /* owner at origin */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_CHILD_OF;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.child_of.use_location_x = true;
    def.params.child_of.use_location_y = true;
    def.params.child_of.use_location_z = true;
    def.params.child_of.use_rotation_x = true;
    def.params.child_of.use_rotation_y = true;
    def.params.child_of.use_rotation_z = true;
    def.params.child_of.use_scale_x = true;
    def.params.child_of.use_scale_y = true;
    def.params.child_of.use_scale_z = true;
    def.params.child_of.inverse_matrix = mat4_identity();

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_CHILD_OF);
    fn(&def, &ctx, &pose[1]);

    /* Owner should now be at target position. */
    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 10.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 0.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_child_of_with_inverse(void) {
    /* Set inverse at target=(5,0,0). Then target moves to (10,0,0).
     * Owner should be at relative offset: (10-5, 0, 0) = (5,0,0). */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(10.0f, 0.0f, 0.0f); /* target now */
    pose[1] = mat4_identity(); /* owner */

    /* inverse_matrix is the inverse of target's transform when "set inverse" was clicked.
     * At that time, target was at (5,0,0), so inverse = translation(-5,0,0). */
    mat4_t inv = mat4_translation(-5.0f, 0.0f, 0.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_CHILD_OF;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.child_of.use_location_x = true;
    def.params.child_of.use_location_y = true;
    def.params.child_of.use_location_z = true;
    def.params.child_of.use_rotation_x = true;
    def.params.child_of.use_rotation_y = true;
    def.params.child_of.use_rotation_z = true;
    def.params.child_of.use_scale_x = true;
    def.params.child_of.use_scale_y = true;
    def.params.child_of.use_scale_z = true;
    def.params.child_of.inverse_matrix = inv;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_CHILD_OF);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 5.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 0.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_child_of_partial_channels(void) {
    /* Only use location_x, not y or z. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(10.0f, 20.0f, 30.0f); /* target */
    pose[1] = mat4_translation(1.0f, 2.0f, 3.0f); /* owner */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_CHILD_OF;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.child_of.use_location_x = true;
    /* y, z location disabled */
    def.params.child_of.inverse_matrix = mat4_identity();

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_CHILD_OF);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 10.0f, 0.001f); /* applied from target */
    ASSERT_FLOAT_EQ(pos.y, 2.0f, 0.001f);  /* unchanged */
    ASSERT_FLOAT_EQ(pos.z, 3.0f, 0.001f);  /* unchanged */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Pivot constraint ────────────────────────────────────────────── */

static int test_pivot_offset(void) {
    /* Rotate 90° around Z with pivot offset (1,0,0).
     * A point at (0,0,0) should rotate around (1,0,0). */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity(); /* target (unused, pivot uses offset) */
    pose[1] = mat4_rotation_z(1.5707963f); /* 90° rotation */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_PIVOT;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.pivot.offset[0] = 1.0f;
    def.params.pivot.offset[1] = 0.0f;
    def.params.pivot.offset[2] = 0.0f;
    def.params.pivot.rotation_range = 0.0f; /* always active */

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_PIVOT);
    fn(&def, &ctx, &pose[1]);

    /* The bone should be translated to account for rotation around pivot.
     * Origin → translated by -pivot → rotated → translated by +pivot.
     * (-1,0,0) rotated 90° = (0,-1,0) + (1,0,0) = (1,-1,0). */
    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 1.0f, 0.1f);
    ASSERT_FLOAT_EQ(pos.y, -1.0f, 0.1f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Registration ────────────────────────────────────────────────── */

static int test_transform_map_registration(void) {
    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    transform_map_register(&solver);

    constraint_type_t types[] = {
        CONSTRAINT_TRANSFORMATION,
        CONSTRAINT_CHILD_OF,
        CONSTRAINT_PIVOT,
    };

    constraint_eval_fn stub = constraint_solver_get_eval_fn(&solver, CONSTRAINT_IK);
    for (int i = 0; i < 3; i++) {
        constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, types[i]);
        ASSERT_TRUE(fn != stub);
    }

    constraint_solver_destroy(&solver);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_transformation_location_to_location);
    RUN(test_transformation_clamped);
    RUN(test_transformation_extrapolate);
    RUN(test_transformation_zero_range);
    RUN(test_child_of_full);
    RUN(test_child_of_with_inverse);
    RUN(test_child_of_partial_channels);
    RUN(test_pivot_offset);
    RUN(test_transform_map_registration);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
