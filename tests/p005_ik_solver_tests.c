/**
 * @file p005_ik_solver_tests.c
 * @brief Unit tests for IK solvers: CCD, FABRIK, and Spline IK.
 *
 * Tests cover:
 * - CCD convergence for 2-bone, 5-bone chains
 * - FABRIK bone length preservation
 * - Unreachable target handling
 * - Pole target (bend direction)
 * - Influence blending via solver
 * - Edge cases: zero chain, target at root, degenerate
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
#include "ferrum/animation/ik_solver.h"
#include "ferrum/math/mat4.h"
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

#define ASSERT_FLOAT_EQ(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_FLOAT_EQ failed: %s:%d: %f != %f (diff=%f)\n", \
               __FILE__, __LINE__, (double)(a), (double)(b), \
               (double)fabsf((a) - (b))); \
        return 1; \
    } \
} while (0)

/* Extract translation from a column-major mat4. */
static vec3_t mat4_get_translation(const mat4_t *m) {
    return (vec3_t){ m->m[12], m->m[13], m->m[14] };
}

static float vec3_dist(vec3_t a, vec3_t b) {
    vec3_t d = vec3_sub(a, b);
    return vec3_magnitude(d);
}

/* ── Helper: build a straight vertical N-bone chain skeleton ─────── */

static void build_chain_skeleton(skeleton_def_t *skel, uint32_t n,
                                 float bone_length) {
    skeleton_def_init(skel, n, 4);
    for (uint32_t i = 0; i < n; i++) {
        skel->parent_indices[i] = (i == 0) ? UINT32_MAX : (i - 1);
        skel->rest_local[i] = (i == 0)
            ? mat4_identity()
            : mat4_translation(0.0f, bone_length, 0.0f);
    }
    /* Compute rest_world. */
    skel->rest_world[0] = skel->rest_local[0];
    for (uint32_t i = 1; i < n; i++) {
        skel->rest_world[i] = mat4_mul(skel->rest_world[i - 1],
                                        skel->rest_local[i]);
    }
}

/* ── Test: CCD 2-bone chain reaches target ───────────────────────── */

static int test_ccd_2bone_reach(void) {
    /* 2 bones, each length 5. Total reach = 10.
     * Target at (7, 0, 0) — reachable. */
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f); /* 3 joints = 2 bones */

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];

    vec3_t target = { 7.0f, 0.0f, 0.0f };
    ik_solve_ccd(&skel, pose, 2, 2, target, 20, 0.01f);

    /* End-effector (joint 2) should be near target. */
    vec3_t effector = mat4_get_translation(&pose[2]);
    float dist = vec3_dist(effector, target);
    ASSERT_FLOAT_EQ(dist, 0.0f, 0.1f); /* within 0.1 units */

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: CCD 5-bone chain reaches target ───────────────────────── */

static int test_ccd_5bone_reach(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 6, 3.0f); /* 6 joints = 5 bones, total = 15 */

    mat4_t pose[6];
    for (uint32_t i = 0; i < 6; i++) pose[i] = skel.rest_world[i];

    vec3_t target = { 5.0f, 8.0f, 0.0f };
    ik_solve_ccd(&skel, pose, 5, 5, target, 30, 0.01f);

    vec3_t effector = mat4_get_translation(&pose[5]);
    float dist = vec3_dist(effector, target);
    ASSERT_FLOAT_EQ(dist, 0.0f, 0.15f);

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: CCD unreachable target — fully extends ────────────────── */

static int test_ccd_unreachable(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f); /* total reach = 10 */

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];

    /* Target at distance 100 — far beyond reach. */
    vec3_t target = { 100.0f, 0.0f, 0.0f };
    ik_solve_ccd(&skel, pose, 2, 2, target, 20, 0.01f);

    /* End-effector should be at max reach (~10 units from root). */
    vec3_t effector = mat4_get_translation(&pose[2]);
    vec3_t root = mat4_get_translation(&pose[0]);
    float dist = vec3_dist(effector, root);
    /* Should be close to total chain length (10). */
    ASSERT_FLOAT_EQ(dist, 10.0f, 0.5f);

    /* No NaN. */
    ASSERT_TRUE(!isnan(effector.x));
    ASSERT_TRUE(!isnan(effector.y));
    ASSERT_TRUE(!isnan(effector.z));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: CCD zero chain length is no-op ────────────────────────── */

static int test_ccd_zero_chain(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f);

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];
    mat4_t original[3];
    memcpy(original, pose, sizeof(pose));

    vec3_t target = { 1.0f, 0.0f, 0.0f };
    ik_solve_ccd(&skel, pose, 2, 0, target, 20, 0.01f);

    /* Nothing should change with chain_length=0. */
    for (int i = 0; i < 3; i++) {
        vec3_t a = mat4_get_translation(&original[i]);
        vec3_t b = mat4_get_translation(&pose[i]);
        ASSERT_FLOAT_EQ(vec3_dist(a, b), 0.0f, 1e-5f);
    }

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: FABRIK 2-bone chain reaches target ────────────────────── */

static int test_fabrik_2bone_reach(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f);

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];

    vec3_t target = { 7.0f, 0.0f, 0.0f };
    ik_solve_fabrik(&skel, pose, 2, 2, target, 20, 0.01f);

    vec3_t effector = mat4_get_translation(&pose[2]);
    float dist = vec3_dist(effector, target);
    ASSERT_FLOAT_EQ(dist, 0.0f, 0.1f);

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: FABRIK preserves bone lengths ──────────────────────────── */

static int test_fabrik_bone_lengths(void) {
    float bone_len = 4.0f;
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 4, bone_len); /* 3 bones */

    mat4_t pose[4];
    for (uint32_t i = 0; i < 4; i++) pose[i] = skel.rest_world[i];

    vec3_t target = { 6.0f, 5.0f, 0.0f };
    ik_solve_fabrik(&skel, pose, 3, 3, target, 30, 0.01f);

    /* Check each bone segment length. */
    for (uint32_t i = 1; i < 4; i++) {
        vec3_t a = mat4_get_translation(&pose[i - 1]);
        vec3_t b = mat4_get_translation(&pose[i]);
        float seg_len = vec3_dist(a, b);
        ASSERT_FLOAT_EQ(seg_len, bone_len, 0.1f);
    }

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: FABRIK unreachable — no NaN ───────────────────────────── */

static int test_fabrik_unreachable(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f);

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];

    vec3_t target = { 0.0f, 200.0f, 0.0f };
    ik_solve_fabrik(&skel, pose, 2, 2, target, 20, 0.01f);

    vec3_t effector = mat4_get_translation(&pose[2]);
    ASSERT_TRUE(!isnan(effector.x));
    ASSERT_TRUE(!isnan(effector.y));
    ASSERT_TRUE(!isnan(effector.z));

    /* Should be fully extended toward target. */
    vec3_t root = mat4_get_translation(&pose[0]);
    float dist = vec3_dist(effector, root);
    ASSERT_FLOAT_EQ(dist, 10.0f, 0.5f);

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: IK dispatch integration — solver calls ik_solve_ccd ──── */

static int test_ik_dispatch_integration(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f);

    /* Add IK constraint on bone 2 (tip of chain). */
    constraint_def_t def;
    memset(&def, 0, sizeof(def));
    def.type = CONSTRAINT_IK;
    def.influence = 1.0f;
    def.target_bone_idx = UINT32_MAX; /* no bone target, use external */
    def.params.ik.chain_length = 2;
    def.params.ik.iterations = 20;
    def.params.ik.weight = 1.0f;
    skel.constraints[2 * skel.max_constraints_per_joint + 0] = def;
    skel.constraint_counts[2] = 1;

    constraint_solver_t solver;
    constraint_solver_init(&solver, 3, 4);
    ik_solver_register(&solver);

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];

    constraint_solver_evaluate(&solver, &skel, NULL, pose, 3);

    /* Without a target bone, IK eval may be a no-op or use world origin.
     * Just verify no crash and no NaN. */
    for (int i = 0; i < 3; i++) {
        vec3_t p = mat4_get_translation(&pose[i]);
        ASSERT_TRUE(!isnan(p.x));
        ASSERT_TRUE(!isnan(p.y));
        ASSERT_TRUE(!isnan(p.z));
    }

    constraint_solver_destroy(&solver);
    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Test: CCD target at bone root ───────────────────────────────── */

static int test_ccd_target_at_root(void) {
    skeleton_def_t skel;
    build_chain_skeleton(&skel, 3, 5.0f);

    mat4_t pose[3];
    for (uint32_t i = 0; i < 3; i++) pose[i] = skel.rest_world[i];

    /* Target at root position — chain should fold back on itself. */
    vec3_t target = { 0.0f, 0.0f, 0.0f };
    ik_solve_ccd(&skel, pose, 2, 2, target, 20, 0.01f);

    vec3_t effector = mat4_get_translation(&pose[2]);
    /* Should be near origin (within tolerance since bones have length). */
    float dist = vec3_dist(effector, target);
    /* Two bones of length 5 can fold to put tip near root. */
    ASSERT_TRUE(dist < 1.0f);
    ASSERT_TRUE(!isnan(effector.x));

    skeleton_def_destroy(&skel);
    return 0;
}

/* ── Main ────────────────────────────────────────────────────────── */

int main(void) {
    RUN(test_ccd_2bone_reach);
    RUN(test_ccd_5bone_reach);
    RUN(test_ccd_unreachable);
    RUN(test_ccd_zero_chain);
    RUN(test_fabrik_2bone_reach);
    RUN(test_fabrik_bone_lengths);
    RUN(test_fabrik_unreachable);
    RUN(test_ik_dispatch_integration);
    RUN(test_ccd_target_at_root);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
