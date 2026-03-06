/**
 * @file p005_limit_tests.c
 * @brief Tests for Limit Rotation, Limit Location, and Limit Scale constraints.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/limit_constraints.h"
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

/* ── Limit Location tests ────────────────────────────────────────── */

static int test_limit_location_within_range(void) {
    /* Position within limits — no change. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_translation(5.0f, 5.0f, 5.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_location.use_min_x = true;
    def.params.limit_location.use_max_x = true;
    def.params.limit_location.min_x = 0.0f;
    def.params.limit_location.max_x = 10.0f;
    def.params.limit_location.use_min_y = true;
    def.params.limit_location.use_max_y = true;
    def.params.limit_location.min_y = 0.0f;
    def.params.limit_location.max_y = 10.0f;
    def.params.limit_location.use_min_z = true;
    def.params.limit_location.use_max_z = true;
    def.params.limit_location.min_z = 0.0f;
    def.params.limit_location.max_z = 10.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 5.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 5.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 5.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_limit_location_clamp_max(void) {
    /* Position beyond max_x — clamped. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_translation(15.0f, 5.0f, 5.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_location.use_max_x = true;
    def.params.limit_location.max_x = 10.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 10.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 5.0f, 0.001f); /* untouched */
    ASSERT_FLOAT_EQ(pos.z, 5.0f, 0.001f); /* untouched */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_limit_location_clamp_min(void) {
    /* Position below min_y — clamped. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_translation(0.0f, -5.0f, 0.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_location.use_min_y = true;
    def.params.limit_location.min_y = 0.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 0.0f, 0.001f); /* clamped from -5 to 0 */
    ASSERT_FLOAT_EQ(pos.z, 0.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_limit_location_independent_axes(void) {
    /* Only limit Z max, leave X and Y alone. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_translation(100.0f, 200.0f, 50.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_location.use_max_z = true;
    def.params.limit_location.max_z = 10.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 100.0f, 0.001f); /* untouched */
    ASSERT_FLOAT_EQ(pos.y, 200.0f, 0.001f); /* untouched */
    ASSERT_FLOAT_EQ(pos.z, 10.0f, 0.001f);  /* clamped */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Limit Rotation tests ────────────────────────────────────────── */

static int test_limit_rotation_within_range(void) {
    /* Small rotation within limits — no change. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    float angle = 0.3f; /* ~17° */
    pose[1] = mat4_rotation_z(angle);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_ROTATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_rotation.use_limit_z = true;
    def.params.limit_rotation.min_z = -1.0f; /* ~-57° */
    def.params.limit_rotation.max_z = 1.0f;  /* ~57° */

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_ROTATION);
    mat4_t before = pose[1];
    fn(&def, &ctx, &pose[1]);

    /* Should be unchanged (within limits). */
    for (int i = 0; i < 16; i++) {
        ASSERT_FLOAT_EQ(pose[1].m[i], before.m[i], 0.01f);
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_limit_rotation_clamp(void) {
    /* Large rotation beyond limit — should be clamped. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    float angle = 2.0f; /* ~115° */
    pose[1] = mat4_rotation_z(angle);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_ROTATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_rotation.use_limit_z = true;
    def.params.limit_rotation.min_z = -1.0f; /* ~-57° */
    def.params.limit_rotation.max_z = 1.0f;  /* ~57° */

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_ROTATION);
    fn(&def, &ctx, &pose[1]);

    /* Check that Z rotation is now clamped to max_z (1.0 rad).
     * Extract Z angle from the rotation matrix: atan2(m[1], m[0]). */
    float clamped_z = atan2f(pose[1].m[1], pose[1].m[0]);
    ASSERT_FLOAT_EQ(clamped_z, 1.0f, 0.05f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_limit_rotation_no_limit_axes(void) {
    /* No axes limited — should be a no-op. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_rotation_z(3.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_ROTATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    /* No use_limit flags set. */

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_ROTATION);
    mat4_t before = pose[1];
    fn(&def, &ctx, &pose[1]);

    for (int i = 0; i < 16; i++) {
        ASSERT_FLOAT_EQ(pose[1].m[i], before.m[i], 0.001f);
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Limit Scale tests ───────────────────────────────────────────── */

static int test_limit_scale_clamp(void) {
    /* Scale (5,0.1,3) clamped to min=(0.5,0.5,0.5) max=(2,2,2). */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_scaling(5.0f, 0.1f, 3.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_SCALE;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_scale.use_min_x = true;
    def.params.limit_scale.use_max_x = true;
    def.params.limit_scale.min_x = 0.5f;
    def.params.limit_scale.max_x = 2.0f;
    def.params.limit_scale.use_min_y = true;
    def.params.limit_scale.use_max_y = true;
    def.params.limit_scale.min_y = 0.5f;
    def.params.limit_scale.max_y = 2.0f;
    def.params.limit_scale.use_min_z = true;
    def.params.limit_scale.use_max_z = true;
    def.params.limit_scale.min_z = 0.5f;
    def.params.limit_scale.max_z = 2.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_SCALE);
    fn(&def, &ctx, &pose[1]);

    /* Extract scale from column lengths. */
    float sx = sqrtf(pose[1].m[0]*pose[1].m[0] + pose[1].m[1]*pose[1].m[1] + pose[1].m[2]*pose[1].m[2]);
    float sy = sqrtf(pose[1].m[4]*pose[1].m[4] + pose[1].m[5]*pose[1].m[5] + pose[1].m[6]*pose[1].m[6]);
    float sz = sqrtf(pose[1].m[8]*pose[1].m[8] + pose[1].m[9]*pose[1].m[9] + pose[1].m[10]*pose[1].m[10]);
    ASSERT_FLOAT_EQ(sx, 2.0f, 0.01f);  /* clamped from 5 to 2 */
    ASSERT_FLOAT_EQ(sy, 0.5f, 0.01f);  /* clamped from 0.1 to 0.5 */
    ASSERT_FLOAT_EQ(sz, 2.0f, 0.01f);  /* clamped from 3 to 2 */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_limit_scale_within_range(void) {
    /* Scale within limits — no change. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_scaling(1.5f, 1.5f, 1.5f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LIMIT_SCALE;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.limit_scale.use_min_x = true;
    def.params.limit_scale.use_max_x = true;
    def.params.limit_scale.min_x = 0.5f;
    def.params.limit_scale.max_x = 2.0f;
    def.params.limit_scale.use_min_y = true;
    def.params.limit_scale.use_max_y = true;
    def.params.limit_scale.min_y = 0.5f;
    def.params.limit_scale.max_y = 2.0f;
    def.params.limit_scale.use_min_z = true;
    def.params.limit_scale.use_max_z = true;
    def.params.limit_scale.min_z = 0.5f;
    def.params.limit_scale.max_z = 2.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LIMIT_SCALE);
    mat4_t before = pose[1];
    fn(&def, &ctx, &pose[1]);

    for (int i = 0; i < 16; i++) {
        ASSERT_FLOAT_EQ(pose[1].m[i], before.m[i], 0.001f);
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Registration test ───────────────────────────────────────────── */

static int test_limit_registration(void) {
    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    limit_constraints_register(&solver);

    constraint_type_t types[] = {
        CONSTRAINT_LIMIT_ROTATION,
        CONSTRAINT_LIMIT_LOCATION,
        CONSTRAINT_LIMIT_SCALE,
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
    RUN(test_limit_location_within_range);
    RUN(test_limit_location_clamp_max);
    RUN(test_limit_location_clamp_min);
    RUN(test_limit_location_independent_axes);
    RUN(test_limit_rotation_within_range);
    RUN(test_limit_rotation_clamp);
    RUN(test_limit_rotation_no_limit_axes);
    RUN(test_limit_scale_clamp);
    RUN(test_limit_scale_within_range);
    RUN(test_limit_registration);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
