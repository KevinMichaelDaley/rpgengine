/**
 * @file p005_surface_vol_tests.c
 * @brief Tests for Floor, Clamp To, Shrinkwrap, and Maintain Volume constraints.
 *
 * Shrinkwrap is stubbed (requires mesh query infrastructure).
 * Clamp To uses simplified nearest-point-on-polyline logic.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/surface_vol.h"
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

/* ── Floor constraint tests ──────────────────────────────────────── */

static int test_floor_above(void) {
    /* Owner above floor — no change. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity(); /* floor at y=0 */
    pose[1] = mat4_translation(0.0f, 5.0f, 0.0f); /* owner above */

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_FLOOR;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.floor.offset = 0.0f;
    def.params.floor.floor_location = CONSTRAINT_FLOOR_BELOW_NEG_Y;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_FLOOR);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.y, 5.0f, 0.001f); /* unchanged */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_floor_below(void) {
    /* Owner below floor — clamped to floor. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_translation(3.0f, -2.0f, 1.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_FLOOR;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.floor.offset = 0.0f;
    def.params.floor.floor_location = CONSTRAINT_FLOOR_BELOW_NEG_Y;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_FLOOR);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.x, 3.0f, 0.001f);  /* unchanged */
    ASSERT_FLOAT_EQ(pos.y, 0.0f, 0.001f);   /* clamped to floor */
    ASSERT_FLOAT_EQ(pos.z, 1.0f, 0.001f);   /* unchanged */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_floor_with_offset(void) {
    /* Floor at y=0, offset=1.5. Owner at y=1.0 — below offset threshold. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_translation(0.0f, 1.0f, 0.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_FLOOR;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.floor.offset = 1.5f;
    def.params.floor.floor_location = CONSTRAINT_FLOOR_BELOW_NEG_Y;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_FLOOR);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.y, 1.5f, 0.001f); /* clamped to floor + offset */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_floor_elevated_target(void) {
    /* Target at y=5 (elevated floor). Owner at y=3 — below floor. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_translation(0.0f, 5.0f, 0.0f); /* floor at y=5 */
    pose[1] = mat4_translation(0.0f, 3.0f, 0.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_FLOOR;
    def.influence = 1.0f;
    def.target_bone_idx = 0;
    def.params.floor.offset = 0.0f;
    def.params.floor.floor_location = CONSTRAINT_FLOOR_BELOW_NEG_Y;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_FLOOR);
    fn(&def, &ctx, &pose[1]);

    vec3_t pos = get_translation(&pose[1]);
    ASSERT_FLOAT_EQ(pos.y, 5.0f, 0.001f); /* clamped to elevated floor */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Maintain Volume tests ───────────────────────────────────────── */

static int test_maintain_volume_stretch_y(void) {
    /* Free axis = Y, scale Y by 4. Other axes should scale by 1/sqrt(4) = 0.5. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_scaling(1.0f, 4.0f, 1.0f);

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_MAINTAIN_VOLUME;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.maintain_volume.free_axis = CONSTRAINT_AXIS_Y;
    def.params.maintain_volume.volume = 1.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_MAINTAIN_VOLUME);
    fn(&def, &ctx, &pose[1]);

    float sx = sqrtf(pose[1].m[0]*pose[1].m[0] + pose[1].m[1]*pose[1].m[1] + pose[1].m[2]*pose[1].m[2]);
    float sy = sqrtf(pose[1].m[4]*pose[1].m[4] + pose[1].m[5]*pose[1].m[5] + pose[1].m[6]*pose[1].m[6]);
    float sz = sqrtf(pose[1].m[8]*pose[1].m[8] + pose[1].m[9]*pose[1].m[9] + pose[1].m[10]*pose[1].m[10]);

    ASSERT_FLOAT_EQ(sy, 4.0f, 0.01f);  /* free axis unchanged */
    ASSERT_FLOAT_EQ(sx, 0.5f, 0.01f);  /* compensated: 1/sqrt(4) = 0.5 */
    ASSERT_FLOAT_EQ(sz, 0.5f, 0.01f);  /* compensated */

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

static int test_maintain_volume_no_scale(void) {
    /* Free axis Y, scale = (1,1,1). No change needed. */
    skeleton_def_t skel; make_skel(&skel);
    mat4_t pose[2];
    pose[0] = mat4_identity();
    pose[1] = mat4_identity();

    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_MAINTAIN_VOLUME;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX;
    def.params.maintain_volume.free_axis = CONSTRAINT_AXIS_Y;
    def.params.maintain_volume.volume = 1.0f;

    constraint_eval_ctx_t ctx = {
        .skel = &skel, .pose = pose, .bone_count = 2, .bone_idx = 1
    };

    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, CONSTRAINT_MAINTAIN_VOLUME);
    mat4_t before = pose[1];
    fn(&def, &ctx, &pose[1]);

    for (int i = 0; i < 16; i++) {
        ASSERT_FLOAT_EQ(pose[1].m[i], before.m[i], 0.001f);
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Registration ────────────────────────────────────────────────── */

static int test_surface_vol_registration(void) {
    constraint_solver_t solver;
    constraint_solver_init(&solver, 2, 4);
    surface_vol_register(&solver);

    constraint_type_t types[] = {
        CONSTRAINT_FLOOR,
        CONSTRAINT_MAINTAIN_VOLUME,
    };

    constraint_eval_fn stub = constraint_solver_get_eval_fn(&solver, CONSTRAINT_IK);
    for (int i = 0; i < 2; i++) {
        constraint_eval_fn fn = constraint_solver_get_eval_fn(&solver, types[i]);
        ASSERT_TRUE(fn != stub);
    }

    constraint_solver_destroy(&solver);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_floor_above);
    RUN(test_floor_below);
    RUN(test_floor_with_offset);
    RUN(test_floor_elevated_target);
    RUN(test_maintain_volume_stretch_y);
    RUN(test_maintain_volume_no_scale);
    RUN(test_surface_vol_registration);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
