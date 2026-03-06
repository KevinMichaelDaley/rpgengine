/**
 * @file p005_constraint_solver_tests.c
 * @brief Unit tests for constraint solver core: space conversion,
 *        influence blending, evaluation order, and dispatch table.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_space.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

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

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_FLOAT_EQ failed: %s:%d: %f != %f\n", \
               __FILE__, __LINE__, (double)(a), (double)(b)); \
        return 1; \
    } \
} while (0)

static int mat4_approx_eq(mat4_t a, mat4_t b, float eps) {
    for (int i = 0; i < 16; i++) {
        if (fabsf(a.m[i] - b.m[i]) > eps) return 0;
    }
    return 1;
}

/* ── Helper: build a simple 3-bone skeleton ──────────────────────── */

static void build_3bone_skeleton(skeleton_def_t *skel) {
    skeleton_def_init(skel, 3, 4);

    /* root at origin */
    skel->parent_indices[0] = UINT32_MAX;
    skel->rest_local[0] = mat4_identity();
    skel->rest_world[0] = mat4_identity();

    /* child at (0, 5, 0) relative to parent */
    skel->parent_indices[1] = 0;
    skel->rest_local[1] = mat4_translation(0.0f, 5.0f, 0.0f);
    skel->rest_world[1] = mat4_mul(skel->rest_world[0], skel->rest_local[1]);

    /* grandchild at (0, 3, 0) relative to child */
    skel->parent_indices[2] = 1;
    skel->rest_local[2] = mat4_translation(0.0f, 3.0f, 0.0f);
    skel->rest_world[2] = mat4_mul(skel->rest_world[1], skel->rest_local[2]);
}

/* ── Test: WORLD space conversion is identity ────────────────────── */

static int test_space_world_roundtrip(void) {
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);

    mat4_t input = mat4_translation(1.0f, 2.0f, 3.0f);
    mat4_t world_out;
    constraint_to_world_space(&skel, 1, CONSTRAINT_SPACE_WORLD, &input, &world_out);
    ASSERT_TRUE(mat4_approx_eq(input, world_out, 1e-5f));

    mat4_t local_out;
    constraint_from_world_space(&skel, 1, CONSTRAINT_SPACE_WORLD, &world_out, &local_out);
    ASSERT_TRUE(mat4_approx_eq(input, local_out, 1e-5f));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: LOCAL space conversion round-trip ──────────────────────── */

static int test_space_local_roundtrip(void) {
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);

    /* A local-space transform for bone 1 (child of root). */
    mat4_t local = mat4_translation(1.0f, 0.0f, 0.0f);
    mat4_t world_out;
    constraint_to_world_space(&skel, 1, CONSTRAINT_SPACE_LOCAL, &local, &world_out);

    /* Expected: parent_world(bone0=identity) × local = local itself. */
    ASSERT_TRUE(mat4_approx_eq(local, world_out, 1e-5f));

    /* Round-trip back. */
    mat4_t back;
    constraint_from_world_space(&skel, 1, CONSTRAINT_SPACE_LOCAL, &world_out, &back);
    ASSERT_TRUE(mat4_approx_eq(local, back, 1e-5f));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: LOCAL space for grandchild (non-identity parent) ──────── */

static int test_space_local_grandchild(void) {
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);

    /* Grandchild's parent is bone 1 at world (0,5,0).
     * A local offset of (1,0,0) should become world (1,5,0). */
    mat4_t local = mat4_translation(1.0f, 0.0f, 0.0f);
    mat4_t world_out;
    constraint_to_world_space(&skel, 2, CONSTRAINT_SPACE_LOCAL, &local, &world_out);

    /* parent_world[1] = translation(0,5,0), so result = T(0,5,0) × T(1,0,0) = T(1,5,0). */
    mat4_t expected = mat4_translation(1.0f, 5.0f, 0.0f);
    ASSERT_TRUE(mat4_approx_eq(expected, world_out, 1e-5f));

    /* Round-trip. */
    mat4_t back;
    constraint_from_world_space(&skel, 2, CONSTRAINT_SPACE_LOCAL, &world_out, &back);
    ASSERT_TRUE(mat4_approx_eq(local, back, 1e-5f));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: POSE space conversion round-trip ───────────────────────── */

static int test_space_pose_roundtrip(void) {
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);

    /* Pose space: relative to rest_world. For bone 2 at rest_world = T(0,8,0).
     * A pose-space offset of T(1,0,0) should produce world T(1,8,0). */
    mat4_t pose_local = mat4_translation(1.0f, 0.0f, 0.0f);
    mat4_t world_out;
    constraint_to_world_space(&skel, 2, CONSTRAINT_SPACE_POSE, &pose_local, &world_out);

    mat4_t expected = mat4_translation(1.0f, 8.0f, 0.0f);
    ASSERT_TRUE(mat4_approx_eq(expected, world_out, 1e-5f));

    mat4_t back;
    constraint_from_world_space(&skel, 2, CONSTRAINT_SPACE_POSE, &world_out, &back);
    ASSERT_TRUE(mat4_approx_eq(pose_local, back, 1e-5f));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: BONE space conversion round-trip ───────────────────────── */

static int test_space_bone_roundtrip(void) {
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);

    /* Bone space: relative to bone's own rest LOCAL transform.
     * Bone 2 rest_local = T(0,3,0).
     * A bone-space offset of T(1,0,0) → world = rest_world[2] × rest_local_inv × T(1,0,0)...
     * Actually bone space = rest_local × input, then to_world via parent.
     * Simpler: bone space = rest_local as basis. */
    mat4_t bone_local = mat4_translation(1.0f, 0.0f, 0.0f);
    mat4_t world_out;
    constraint_to_world_space(&skel, 2, CONSTRAINT_SPACE_BONE, &bone_local, &world_out);

    /* Round-trip must recover original. */
    mat4_t back;
    constraint_from_world_space(&skel, 2, CONSTRAINT_SPACE_BONE, &world_out, &back);
    ASSERT_TRUE(mat4_approx_eq(bone_local, back, 1e-4f));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: influence blending at 0.0 (no effect) ─────────────────── */

static int test_influence_zero(void) {
    mat4_t original = mat4_translation(1.0f, 2.0f, 3.0f);
    mat4_t constrained = mat4_translation(10.0f, 20.0f, 30.0f);
    mat4_t result;
    constraint_blend_influence(&original, &constrained, 0.0f, &result);

    /* At influence 0.0, result should be original. */
    ASSERT_TRUE(mat4_approx_eq(original, result, 1e-5f));
    return 0;
}

/* ── Test: influence blending at 1.0 (full effect) ───────────────── */

static int test_influence_one(void) {
    mat4_t original = mat4_translation(1.0f, 2.0f, 3.0f);
    mat4_t constrained = mat4_translation(10.0f, 20.0f, 30.0f);
    mat4_t result;
    constraint_blend_influence(&original, &constrained, 1.0f, &result);

    /* At influence 1.0, result should be constrained. */
    ASSERT_TRUE(mat4_approx_eq(constrained, result, 1e-5f));
    return 0;
}

/* ── Test: influence blending at 0.5 (halfway) ───────────────────── */

static int test_influence_half(void) {
    mat4_t original = mat4_translation(0.0f, 0.0f, 0.0f);
    mat4_t constrained = mat4_translation(10.0f, 0.0f, 0.0f);
    mat4_t result;
    constraint_blend_influence(&original, &constrained, 0.5f, &result);

    /* Translation should be halfway: (5, 0, 0). */
    ASSERT_FLOAT_EQ(result.m[12], 5.0f, 1e-4f);
    ASSERT_FLOAT_EQ(result.m[13], 0.0f, 1e-4f);
    ASSERT_FLOAT_EQ(result.m[14], 0.0f, 1e-4f);
    return 0;
}

/* ── Test: solver init and destroy ───────────────────────────────── */

static int test_solver_init_destroy(void) {
    constraint_solver_t solver;
    bool ok = constraint_solver_init(&solver, 10, 4);
    ASSERT_TRUE(ok);
    ASSERT_EQ(solver.max_bones, 10);
    ASSERT_EQ(solver.max_constraints_per_bone, 4);

    constraint_solver_destroy(&solver);
    return 0;
}

/* ── Test: solver init with zero bones fails ─────────────────────── */

static int test_solver_init_zero_bones(void) {
    constraint_solver_t solver;
    bool ok = constraint_solver_init(&solver, 0, 4);
    ASSERT_TRUE(!ok);
    return 0;
}

/* ── Test: dispatch table initially has no-op stubs ──────────────── */

static int test_dispatch_stubs(void) {
    constraint_solver_t solver;
    constraint_solver_init(&solver, 4, 2);

    /* All dispatch entries should be non-NULL (no-op stubs). */
    for (int i = 0; i < CONSTRAINT_TYPE_COUNT; i++) {
        constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, (constraint_type_t)i);
        ASSERT_TRUE(fn != NULL);
    }

    constraint_solver_destroy(&solver);
    return 0;
}

/* ── Test: dispatch registration overrides stub ──────────────────── */

static int s_custom_eval_count;

static void custom_eval(const constraint_def_t *def,
                        const constraint_eval_ctx_t *ctx,
                        mat4_t *inout) {
    (void)def; (void)ctx; (void)inout;
    s_custom_eval_count++;
}

static int test_dispatch_register(void) {
    constraint_solver_t solver;
    constraint_solver_init(&solver, 4, 2);

    constraint_solver_register_eval(&solver, CONSTRAINT_LIMIT_ROTATION, custom_eval);
    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_ROTATION);
    ASSERT_TRUE(fn == custom_eval);

    constraint_solver_destroy(&solver);
    return 0;
}

/* ── Test: solver evaluate calls dispatch in correct order ────────── */

static uint32_t s_eval_order[16];
static int s_eval_order_count;

static void tracking_eval(const constraint_def_t *def,
                          const constraint_eval_ctx_t *ctx,
                          mat4_t *inout) {
    (void)def; (void)inout;
    if (s_eval_order_count < 16) {
        s_eval_order[s_eval_order_count++] = ctx->bone_idx;
    }
}

static int test_solver_evaluation_order(void) {
    /* Set up a 3-bone skeleton with constraints on bones 0 and 2. */
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);

    /* Bone 0: one IK constraint. */
    constraint_def_t ik_def;
    memset(&ik_def, 0, sizeof(ik_def));
    ik_def.type = CONSTRAINT_IK;
    ik_def.influence = 1.0f;
    skel.constraints[0 * skel.max_constraints_per_joint + 0] = ik_def;
    skel.constraint_counts[0] = 1;

    /* Bone 2: two constraints (IK then Limit Rotation). */
    constraint_def_t limit_def;
    memset(&limit_def, 0, sizeof(limit_def));
    limit_def.type = CONSTRAINT_LIMIT_ROTATION;
    limit_def.influence = 1.0f;

    skel.constraints[2 * skel.max_constraints_per_joint + 0] = ik_def;
    skel.constraints[2 * skel.max_constraints_per_joint + 1] = limit_def;
    skel.constraint_counts[2] = 2;

    /* Create solver, register tracking eval for both types. */
    constraint_solver_t solver;
    constraint_solver_init(&solver, 3, 4);
    constraint_solver_register_eval(&solver, CONSTRAINT_IK, tracking_eval);
    constraint_solver_register_eval(&solver, CONSTRAINT_LIMIT_ROTATION, tracking_eval);

    /* Prepare pose matrices (just identity). */
    mat4_t pose[3];
    for (int i = 0; i < 3; i++) pose[i] = mat4_identity();

    /* Evaluate. */
    s_eval_order_count = 0;
    constraint_solver_evaluate(&solver, &skel, NULL, pose, 3);

    /* Expected order: bone 0 (1 constraint), bone 2 (2 constraints).
     * Bone 1 has no constraints so no calls.
     * Total: 3 calls. Order: 0, 2, 2. */
    ASSERT_EQ(s_eval_order_count, 3);
    ASSERT_EQ(s_eval_order[0], 0); /* bone 0, constraint 0 */
    ASSERT_EQ(s_eval_order[1], 2); /* bone 2, constraint 0 */
    ASSERT_EQ(s_eval_order[2], 2); /* bone 2, constraint 1 */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: solver evaluate with zero constraints is no-op ────────── */

static int test_solver_no_constraints(void) {
    skeleton_def_t skel;
    build_3bone_skeleton(&skel);
    /* No constraints on any bone. */

    constraint_solver_t solver;
    constraint_solver_init(&solver, 3, 4);

    mat4_t pose[3];
    for (int i = 0; i < 3; i++) pose[i] = mat4_translation((float)i, 0.0f, 0.0f);
    mat4_t original[3];
    memcpy(original, pose, sizeof(pose));

    constraint_solver_evaluate(&solver, &skel, NULL, pose, 3);

    /* Pose should be unchanged. */
    for (int i = 0; i < 3; i++) {
        ASSERT_TRUE(mat4_approx_eq(original[i], pose[i], 1e-5f));
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: solver evaluate respects influence ────────────────────── */

static void offset_eval(const constraint_def_t *def,
                        const constraint_eval_ctx_t *ctx,
                        mat4_t *inout) {
    (void)def; (void)ctx;
    /* Always sets translation to (100, 0, 0). */
    *inout = mat4_translation(100.0f, 0.0f, 0.0f);
}

static int test_solver_influence_blending(void) {
    skeleton_def_t skel;
    skeleton_def_init(&skel, 1, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.rest_local[0] = mat4_identity();
    skel.rest_world[0] = mat4_identity();

    /* Add constraint with influence 0.5. */
    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_FLOOR;
    def.influence = 0.5f;
    skel.constraints[0] = def;
    skel.constraint_counts[0] = 1;

    constraint_solver_t solver;
    constraint_solver_init(&solver, 1, 4);
    constraint_solver_register_eval(&solver, CONSTRAINT_FLOOR, offset_eval);

    /* Start at (0, 0, 0). */
    mat4_t pose[1];
    pose[0] = mat4_identity();
    constraint_solver_evaluate(&solver, &skel, NULL, pose, 1);

    /* offset_eval sets to (100, 0, 0), but influence=0.5 blends:
     * result.x = lerp(0, 100, 0.5) = 50. */
    ASSERT_FLOAT_EQ(pose[0].m[12], 50.0f, 1e-3f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_space_world_roundtrip);
    RUN(test_space_local_roundtrip);
    RUN(test_space_local_grandchild);
    RUN(test_space_pose_roundtrip);
    RUN(test_space_bone_roundtrip);
    RUN(test_influence_zero);
    RUN(test_influence_one);
    RUN(test_influence_half);
    RUN(test_solver_init_destroy);
    RUN(test_solver_init_zero_bones);
    RUN(test_dispatch_stubs);
    RUN(test_dispatch_register);
    RUN(test_solver_evaluation_order);
    RUN(test_solver_no_constraints);
    RUN(test_solver_influence_blending);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
