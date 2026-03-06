/**
 * @file p005_copy_track_tests.c
 * @brief Tests for Copy (Transforms/Rotation/Location/Scale) and
 *        Tracking (Damped Track/Track To/Locked Track) constraints.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/copy_track.h"
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

static vec3_t get_translation(const mat4_t *m) {
    return (vec3_t){ m->m[12], m->m[13], m->m[14] };
}

/* ── Copy Transforms ─────────────────────────────────────────────── */

static int test_copy_transforms_replace(void) {
    /* Owner at origin, target at (5,3,1). Copy replaces owner position. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_local[0] = mat4_identity();
    skel.rest_local[1] = mat4_translation(1.0f, 0.0f, 0.0f);
    skel.rest_world[0] = skel.rest_local[0];
    skel.rest_world[1] = mat4_mul(skel.rest_world[0], skel.rest_local[1]);

    /* Target bone 0 at (5,3,1). */
    mat4_t pose[2];
    pose[0] = mat4_translation(5.0f, 3.0f, 1.0f);
    pose[1] = mat4_translation(1.0f, 0.0f, 0.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_TRANSFORMS;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_transforms.mix_mode = CONSTRAINT_MIX_REPLACE;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    /* Get the eval fn and call it directly. */
    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_TRANSFORMS);
    ASSERT_TRUE(fn != NULL);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 5.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 3.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 1.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_copy_transforms_identity(void) {
    /* If target == identity, owner should become identity. */
    mat4_t target = mat4_identity();
    mat4_t owner = mat4_translation(10.0f, 20.0f, 30.0f);

    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_local[0] = mat4_identity();
    skel.rest_local[1] = mat4_identity();
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2] = { target, owner };

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_TRANSFORMS;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_transforms.mix_mode = CONSTRAINT_MIX_REPLACE;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_TRANSFORMS);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 0.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 0.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Copy Location ───────────────────────────────────────────────── */

static int test_copy_location_full(void) {
    /* Copy all axes from target. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(3.0f, 7.0f, -2.0f);
    pose[1] = mat4_translation(0.0f, 0.0f, 0.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_location.use_x = true;
    def.params.copy_location.use_y = true;
    def.params.copy_location.use_z = true;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 3.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 7.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, -2.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_copy_location_axis_mask(void) {
    /* Copy only Y axis. X and Z should remain unchanged. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(10.0f, 20.0f, 30.0f);
    pose[1] = mat4_translation(1.0f, 2.0f, 3.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_location.use_x = false;
    def.params.copy_location.use_y = true;
    def.params.copy_location.use_z = false;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 1.0f, 0.001f);  /* unchanged */
    ASSERT_FLOAT_EQ(pos.y, 20.0f, 0.001f); /* copied */
    ASSERT_FLOAT_EQ(pos.z, 3.0f, 0.001f);  /* unchanged */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_copy_location_invert(void) {
    /* Copy with inversion on X axis. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(5.0f, 3.0f, 1.0f);
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_location.use_x = true;
    def.params.copy_location.use_y = true;
    def.params.copy_location.use_z = true;
    def.params.copy_location.invert_x = true;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, -5.0f, 0.001f); /* inverted */
    ASSERT_FLOAT_EQ(pos.y, 3.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 1.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_copy_location_offset(void) {
    /* Offset mode: add target position to owner's position. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(1.0f, 2.0f, 3.0f);
    pose[1] = mat4_translation(10.0f, 20.0f, 30.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_location.use_x = true;
    def.params.copy_location.use_y = true;
    def.params.copy_location.use_z = true;
    def.params.copy_location.offset = true;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_LOCATION);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 11.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 22.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 33.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Copy Rotation ───────────────────────────────────────────────── */

static int test_copy_rotation_full(void) {
    /* Copy rotation from target (90° around Z). */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_rotation_z(1.5707963f); /* 90° */
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_ROTATION;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_rotation.use_x = true;
    def.params.copy_rotation.use_y = true;
    def.params.copy_rotation.use_z = true;
    def.params.copy_rotation.mix_mode = CONSTRAINT_MIX_REPLACE;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_ROTATION);
    fn(&def, &ctx, &pose[1]);

    /* After copying rotation, pose[1] upper-3x3 should match pose[0]. */
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            ASSERT_FLOAT_EQ(pose[1].m[i * 4 + j], pose[0].m[i * 4 + j], 0.001f);
        }
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Copy Scale ──────────────────────────────────────────────────── */

static int test_copy_scale_full(void) {
    /* Copy scale from target. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_scaling(2.0f, 3.0f, 0.5f);
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_SCALE;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.copy_scale.use_x = true;
    def.params.copy_scale.use_y = true;
    def.params.copy_scale.use_z = true;
    def.params.copy_scale.power = 1.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_SCALE);
    fn(&def, &ctx, &pose[1]);

    /* Check scale columns. Column 0 length = 2, column 1 = 3, column 2 = 0.5 */
    float sx = sqrtf(pose[1].m[0]*pose[1].m[0] + pose[1].m[1]*pose[1].m[1] + pose[1].m[2]*pose[1].m[2]);
    float sy = sqrtf(pose[1].m[4]*pose[1].m[4] + pose[1].m[5]*pose[1].m[5] + pose[1].m[6]*pose[1].m[6]);
    float sz = sqrtf(pose[1].m[8]*pose[1].m[8] + pose[1].m[9]*pose[1].m[9] + pose[1].m[10]*pose[1].m[10]);
    ASSERT_FLOAT_EQ(sx, 2.0f, 0.01f);
    ASSERT_FLOAT_EQ(sy, 3.0f, 0.01f);
    ASSERT_FLOAT_EQ(sz, 0.5f, 0.01f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Damped Track ────────────────────────────────────────────────── */

static int test_damped_track_y_axis(void) {
    /* Bone at origin, target at (1,0,0). Track Y axis to target.
     * Result: bone's Y axis should point toward (1,0,0). */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(10.0f, 0.0f, 0.0f); /* target */
    pose[1] = mat4_identity(); /* owner at origin */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_DAMPED_TRACK;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.damped_track.track_axis = CONSTRAINT_AXIS_Y;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_DAMPED_TRACK);
    fn(&def, &ctx, &pose[1]);

    /* Y column (m[4], m[5], m[6]) should point toward target.
     * Target is at (10,0,0), owner at (0,0,0), so direction is (1,0,0). */
    vec3_t y_col = { pose[1].m[4], pose[1].m[5], pose[1].m[6] };
    y_col = vec3_normalize_safe(y_col, 1e-7f);
    ASSERT_FLOAT_EQ(y_col.x, 1.0f, 0.01f);
    ASSERT_FLOAT_EQ(y_col.y, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(y_col.z, 0.0f, 0.01f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_damped_track_degenerate(void) {
    /* Target at same position as owner — should not produce NaN. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_identity(); /* target at origin */
    pose[1] = mat4_identity(); /* owner at origin */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_DAMPED_TRACK;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.damped_track.track_axis = CONSTRAINT_AXIS_Y;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_DAMPED_TRACK);
    fn(&def, &ctx, &pose[1]);

    /* No NaN, no crash. */
    ASSERT_TRUE(!isnan(pose[1].m[0]));
    ASSERT_TRUE(!isnan(pose[1].m[5]));
    ASSERT_TRUE(!isnan(pose[1].m[10]));

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Track To ────────────────────────────────────────────────────── */

static int test_track_to_y_up_z(void) {
    /* Track Y axis at target, keep Z axis as up. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(0.0f, 0.0f, 10.0f); /* target */
    pose[1] = mat4_identity(); /* owner at origin */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_TRACK_TO;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.track_to.track_axis = CONSTRAINT_AXIS_Y;
    def.params.track_to.up_axis = CONSTRAINT_AXIS_Z;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_TRACK_TO);
    fn(&def, &ctx, &pose[1]);

    /* Y column should point toward target (0,0,1). */
    vec3_t y_col = { pose[1].m[4], pose[1].m[5], pose[1].m[6] };
    y_col = vec3_normalize_safe(y_col, 1e-7f);
    ASSERT_FLOAT_EQ(y_col.x, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(y_col.y, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(y_col.z, 1.0f, 0.01f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Locked Track ────────────────────────────────────────────────── */

static int test_locked_track_z_lock(void) {
    /* Lock Z axis, track Y at target at (1, 0, 0).
     * Z axis should remain (0,0,1). */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 2, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.parent_indices[1] = 0;
    skel.rest_world[0] = mat4_identity();
    skel.rest_world[1] = mat4_identity();

    mat4_t pose[2];
    pose[0] = mat4_translation(10.0f, 0.0f, 0.0f); /* target */
    pose[1] = mat4_identity(); /* owner at origin */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_LOCKED_TRACK;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.locked_track.track_axis = CONSTRAINT_AXIS_Y;
    def.params.locked_track.lock_axis = CONSTRAINT_AXIS_Z;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_LOCKED_TRACK);
    fn(&def, &ctx, &pose[1]);

    /* Z axis column (m[8], m[9], m[10]) should be preserved as (0,0,1). */
    ASSERT_FLOAT_EQ(pose[1].m[8], 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(pose[1].m[9], 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(pose[1].m[10], 1.0f, 0.01f);

    /* Y column should point toward (1,0,0) projected onto XY plane. */
    vec3_t y_col = { pose[1].m[4], pose[1].m[5], pose[1].m[6] };
    y_col = vec3_normalize_safe(y_col, 1e-7f);
    ASSERT_FLOAT_EQ(y_col.x, 1.0f, 0.01f);
    ASSERT_FLOAT_EQ(y_col.y, 0.0f, 0.01f);
    ASSERT_FLOAT_EQ(y_col.z, 0.0f, 0.01f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Registration ────────────────────────────────────────────────── */

static int test_all_registered(void) {
    /* All 7 types should be registered after copy_track_register(). */
    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    copy_track_register(&solver);

    constraint_type_t types[] = {
        CONSTRAINT_COPY_TRANSFORMS,
        CONSTRAINT_COPY_ROTATION,
        CONSTRAINT_COPY_LOCATION,
        CONSTRAINT_COPY_SCALE,
        CONSTRAINT_DAMPED_TRACK,
        CONSTRAINT_TRACK_TO,
        CONSTRAINT_LOCKED_TRACK,
    };

    for (int i = 0; i < 7; i++) {
        constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, types[i]);
        /* Should NOT be the same as an unregistered type's stub. */
        constraint_eval_fn stub = constraint_solver_get_eval_fn(&solver, CONSTRAINT_IK);
        ASSERT_TRUE(fn != stub);
    }

    constraint_solver_destroy(&solver);
    return 0;
}

/* ── No-target edge case ─────────────────────────────────────────── */

static int test_copy_no_target(void) {
    /* target_bone_idx = UINT32_MAX → no-op. */
    skeleton_def_t skel;
    skeleton_def_init(&skel, 1, 4);
    skel.parent_indices[0] = UINT32_MAX;
    skel.rest_world[0] = mat4_identity();

    mat4_t pose[1];
    pose[0] = mat4_translation(5.0f, 5.0f, 5.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_COPY_LOCATION;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.copy_location.use_x = true;
    def.params.copy_location.use_y = true;
    def.params.copy_location.use_z = true;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 1, .bone_idx = 0
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 1, 4);
    copy_track_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_COPY_LOCATION);
    fn(&def, &ctx, &pose[0]);

    /* Should be unchanged — no valid target. */
    vec3_t pos = get_translation(&pose[0]);
    ASSERT_FLOAT_EQ(pos.x, 5.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.y, 5.0f, 0.001f);
    ASSERT_FLOAT_EQ(pos.z, 5.0f, 0.001f);

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_copy_transforms_replace);
    RUN(test_copy_transforms_identity);
    RUN(test_copy_location_full);
    RUN(test_copy_location_axis_mask);
    RUN(test_copy_location_invert);
    RUN(test_copy_location_offset);
    RUN(test_copy_rotation_full);
    RUN(test_copy_scale_full);
    RUN(test_damped_track_y_axis);
    RUN(test_damped_track_degenerate);
    RUN(test_track_to_y_up_z);
    RUN(test_locked_track_z_lock);
    RUN(test_all_registered);
    RUN(test_copy_no_target);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
