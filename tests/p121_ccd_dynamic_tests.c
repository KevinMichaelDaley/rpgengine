/**
 * @file p121_ccd_dynamic_tests.c
 * @brief Tests for dynamic-dynamic CCD (swept convex vs convex with manifold output).
 *
 * Validates that phys_stage_ccd_dynamic correctly detects tunneling between
 * two fast-moving dynamic primitives and produces solver-ready manifolds.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/ccd_dynamic.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/phys_pool.h"

/* ── Minimal test harness ──────────────────────────────────────── */

static int g_pass, g_fail;
static const char *g_current_test;

#define RUN(fn) do { \
    g_current_test = #fn; \
    int _r = fn(); \
    if (_r == 0) { g_pass++; printf("  %-60s [OK]\n", #fn); } \
    else         { g_fail++; printf("  %-60s [FAIL]\n", #fn); } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        printf("ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("ASSERT_EQ failed: %s:%d: %d vs %d\n", __FILE__, __LINE__, \
               (int)(a), (int)(b)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        printf("ASSERT_FLOAT_NEAR failed: %s:%d: %f vs %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)_e); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_GT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a > _b)) { \
        printf("ASSERT_FLOAT_GT failed: %s:%d: %f > %f\n", \
               __FILE__, __LINE__, (double)_a, (double)_b); \
        return 1; \
    } \
} while (0)

/* ── Helpers ───────────────────────────────────────────────────── */

/** Identity quaternion. */
static const phys_quat_t QUAT_ID = {0, 0, 0, 1};

/** Initialize a dynamic body with CCD at a position. */
static void make_dynamic_ccd(phys_body_t *b, phys_vec3_t pos) {
    phys_body_init(b);
    b->position = pos;
    b->orientation = QUAT_ID;
    b->inv_mass = 1.0f;
    b->inv_inertia_diag = (phys_vec3_t){1, 1, 1};
    b->flags = PHYS_BODY_FLAG_CCD;
    b->tier = 0;
    b->friction = 0.5f;
    b->restitution = 0.3f;
}

/* ── Test: head-on sphere collision ────────────────────────────── */

/**
 * Two unit spheres moving toward each other:
 *   prev: A at (-5,0,0), B at (+5,0,0)
 *   curr: A at (+1,0,0), B at (-1,0,0)
 * They should pass through each other without CCD but be caught with CCD.
 * Expect 1 manifold with correct normal (roughly along X axis).
 */
static int test_sphere_vs_sphere_headon(void) {
    /* Body arrays: index 0 = A, index 1 = B, index 2 = sentinel (unused). */
    phys_body_t prev[3], curr[3];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){-5, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){ 5, 0, 0});

    make_dynamic_ccd(&curr[0], (phys_vec3_t){ 1, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){-1, 0, 0});

    /* Colliders: both spheres with radius=1. */
    phys_collider_t colliders[3];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_SPHERE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 1;

    phys_sphere_t spheres[2] = {{1.0f}, {1.0f}};

    /* Broadphase pair: (0, 1). */
    phys_collision_pair_t pairs[1] = {{0, 1}};

    /* Output manifolds. */
    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    /* Should produce exactly 1 manifold. */
    ASSERT_EQ(manifold_count, 1);
    ASSERT_TRUE(manifolds[0].point_count >= 1);

    /* Normal should be roughly along X axis (A→B). */
    float nx = fabsf(manifolds[0].points[0].normal.x);
    ASSERT_FLOAT_GT(nx, 0.8f);

    /* Penetration should be positive. */
    ASSERT_FLOAT_GT(manifolds[0].points[0].penetration, 0.0f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: no collision when spheres don't cross paths ─────────── */

/**
 * Two spheres moving in parallel — should produce no manifolds.
 *   prev: A at (0,0,0), B at (0,5,0)
 *   curr: A at (10,0,0), B at (10,5,0)
 */
static int test_sphere_parallel_no_collision(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){0, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){0, 5, 0});
    make_dynamic_ccd(&curr[0], (phys_vec3_t){10, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){10, 5, 0});

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_SPHERE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 1;

    phys_sphere_t spheres[2] = {{1.0f}, {1.0f}};
    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    ASSERT_EQ(manifold_count, 0);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: capsule vs box tunneling ────────────────────────────── */

/**
 * A capsule sweeping through a box:
 *   prev: capsule at (-5,0,0), box at (0,0,0)
 *   curr: capsule at (+5,0,0), box at (0,0,0)  (box is CCD but stationary)
 * Capsule radius=0.3, half_height=0.5.  Box half_extents=(1,1,1).
 * Should detect the crossing and produce a manifold.
 */
static int test_capsule_vs_box_tunneling(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){-5, 0, 0});  /* capsule */
    make_dynamic_ccd(&prev[1], (phys_vec3_t){ 0, 0, 0});  /* box */

    make_dynamic_ccd(&curr[0], (phys_vec3_t){ 5, 0, 0});  /* capsule moved */
    make_dynamic_ccd(&curr[1], (phys_vec3_t){ 0, 0, 0});  /* box stationary */

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_CAPSULE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_BOX;
    colliders[1].shape_index = 0;

    phys_capsule_t capsules[1] = {{0.3f, 0.5f}};
    phys_box_t boxes[1] = {{{1, 1, 1}}};

    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = NULL,
        .capsules    = capsules,
        .boxes       = boxes,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    ASSERT_EQ(manifold_count, 1);
    ASSERT_TRUE(manifolds[0].point_count >= 1);

    /* Contact normal should be roughly along X axis. */
    float nx = fabsf(manifolds[0].points[0].normal.x);
    ASSERT_FLOAT_GT(nx, 0.7f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: already overlapping at t=0 ──────────────────────────── */

/**
 * Two spheres already overlapping at prev:
 *   prev: A at (0,0,0), B at (1,0,0)   (centers 1.0 apart, radii 1.0 each)
 *   curr: same positions
 * Should detect overlap immediately (TOI ~ 0) and produce manifold.
 */
static int test_already_overlapping(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){0, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){1, 0, 0});

    make_dynamic_ccd(&curr[0], (phys_vec3_t){0, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){1, 0, 0});

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_SPHERE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 1;

    phys_sphere_t spheres[2] = {{1.0f}, {1.0f}};
    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    /* Already overlapping → should produce a manifold. */
    ASSERT_EQ(manifold_count, 1);
    ASSERT_TRUE(manifolds[0].point_count >= 1);
    ASSERT_FLOAT_GT(manifolds[0].points[0].penetration, 0.0f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: skips static bodies ─────────────────────────────────── */

/**
 * If one body is static (inv_mass == 0), dynamic-dynamic CCD should skip it
 * (static CCD handles that case separately).
 */
static int test_skips_static_body(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){-5, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){ 0, 0, 0});
    prev[1].inv_mass = 0.0f;  /* static */
    prev[1].flags |= PHYS_BODY_FLAG_STATIC;

    make_dynamic_ccd(&curr[0], (phys_vec3_t){ 5, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){ 0, 0, 0});
    curr[1].inv_mass = 0.0f;
    curr[1].flags |= PHYS_BODY_FLAG_STATIC;

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_SPHERE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 1;

    phys_sphere_t spheres[2] = {{1.0f}, {1.0f}};
    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    /* Should produce 0 manifolds — static pair skipped. */
    ASSERT_EQ(manifold_count, 0);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: skips mesh shapes ───────────────────────────────────── */

/**
 * Mesh shapes don't have SDF support functions; dynamic-dynamic CCD
 * should skip pairs involving mesh colliders.
 */
static int test_skips_mesh_shapes(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){-5, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){ 0, 0, 0});
    make_dynamic_ccd(&curr[0], (phys_vec3_t){ 5, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){ 0, 0, 0});

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_SPHERE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_MESH;
    colliders[1].shape_index = 0;

    phys_sphere_t spheres[1] = {{1.0f}};
    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    ASSERT_EQ(manifold_count, 0);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: requires at least one CCD-flagged body ──────────────── */

/**
 * If neither body has PHYS_BODY_FLAG_CCD, the pair is skipped.
 */
static int test_requires_ccd_flag(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){-5, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){ 5, 0, 0});
    prev[0].flags &= ~(uint32_t)PHYS_BODY_FLAG_CCD;
    prev[1].flags &= ~(uint32_t)PHYS_BODY_FLAG_CCD;

    make_dynamic_ccd(&curr[0], (phys_vec3_t){ 1, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){-1, 0, 0});
    curr[0].flags &= ~(uint32_t)PHYS_BODY_FLAG_CCD;
    curr[1].flags &= ~(uint32_t)PHYS_BODY_FLAG_CCD;

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_SPHERE;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 1;

    phys_sphere_t spheres[2] = {{1.0f}, {1.0f}};
    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    ASSERT_EQ(manifold_count, 0);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: with rotation (slerp orientations) ──────────────────── */

/**
 * A box rotated 90° around Y between prev and curr such that its corner
 * sweeps into an adjacent sphere.
 *   prev: box at (0,0,0) facing default, sphere at (2,0,0)
 *   curr: box at (0,0,0) rotated 45° Y, sphere at (2,0,0)
 * Box half_extents=(1.5,0.5,0.5) — the long edge sweeps past the sphere.
 * Sphere radius=0.5.
 */
static int test_rotation_sweep(void) {
    phys_body_t prev[2], curr[2];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    /* Box at origin, identity rotation. */
    make_dynamic_ccd(&prev[0], (phys_vec3_t){0, 0, 0});
    /* Sphere at (2,0,0). */
    make_dynamic_ccd(&prev[1], (phys_vec3_t){2, 0, 0});

    /* Box: rotated 45° around Y.
     * quat for 45° Y rotation: (0, sin(π/8), 0, cos(π/8)). */
    float half_angle = 3.14159265f / 4.0f;  /* 45° */
    float s = sinf(half_angle / 2.0f);
    float c = cosf(half_angle / 2.0f);
    make_dynamic_ccd(&curr[0], (phys_vec3_t){0, 0, 0});
    curr[0].orientation = (phys_quat_t){0, s, 0, c};

    make_dynamic_ccd(&curr[1], (phys_vec3_t){2, 0, 0});

    phys_collider_t colliders[2];
    memset(colliders, 0, sizeof(colliders));
    colliders[0].type = PHYS_SHAPE_BOX;
    colliders[0].shape_index = 0;
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 0;

    phys_box_t boxes[1] = {{{1.5f, 0.5f, 0.5f}}};
    phys_sphere_t spheres[1] = {{0.5f}};

    phys_collision_pair_t pairs[1] = {{0, 1}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = boxes,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 2,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    /* The box corner sweeps past the sphere → should detect contact. */
    ASSERT_EQ(manifold_count, 1);
    ASSERT_TRUE(manifolds[0].point_count >= 1);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: manifold carries correct body indices ───────────────── */

/**
 * Verify manifold body_a and body_b are set correctly from the pair.
 */
static int test_manifold_body_indices(void) {
    phys_body_t prev[4], curr[4];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    /* Use indices 1 and 3 (not 0-based consecutive). */
    make_dynamic_ccd(&prev[1], (phys_vec3_t){-5, 0, 0});
    make_dynamic_ccd(&prev[3], (phys_vec3_t){ 5, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){ 1, 0, 0});
    make_dynamic_ccd(&curr[3], (phys_vec3_t){-1, 0, 0});

    phys_collider_t colliders[4];
    memset(colliders, 0, sizeof(colliders));
    colliders[1].type = PHYS_SHAPE_SPHERE;
    colliders[1].shape_index = 0;
    colliders[3].type = PHYS_SHAPE_SPHERE;
    colliders[3].shape_index = 1;

    phys_sphere_t spheres[2] = {{1.0f}, {1.0f}};
    phys_collision_pair_t pairs[1] = {{1, 3}};

    phys_manifold_t manifolds[4];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 1,
        .body_count  = 4,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 4,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    ASSERT_EQ(manifold_count, 1);
    ASSERT_EQ(manifolds[0].body_a, 1);
    ASSERT_EQ(manifolds[0].body_b, 3);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: max manifolds cap ───────────────────────────────────── */

/**
 * When max_manifolds is reached, stop emitting (don't overflow).
 */
static int test_max_manifolds_cap(void) {
    /* 2 pairs, but max_manifolds = 1. */
    phys_body_t prev[4], curr[4];
    memset(prev, 0, sizeof(prev));
    memset(curr, 0, sizeof(curr));

    make_dynamic_ccd(&prev[0], (phys_vec3_t){-5, 0, 0});
    make_dynamic_ccd(&prev[1], (phys_vec3_t){ 5, 0, 0});
    make_dynamic_ccd(&prev[2], (phys_vec3_t){-5, 10, 0});
    make_dynamic_ccd(&prev[3], (phys_vec3_t){ 5, 10, 0});

    make_dynamic_ccd(&curr[0], (phys_vec3_t){ 1, 0, 0});
    make_dynamic_ccd(&curr[1], (phys_vec3_t){-1, 0, 0});
    make_dynamic_ccd(&curr[2], (phys_vec3_t){ 1, 10, 0});
    make_dynamic_ccd(&curr[3], (phys_vec3_t){-1, 10, 0});

    phys_collider_t colliders[4];
    memset(colliders, 0, sizeof(colliders));
    for (int i = 0; i < 4; i++) {
        colliders[i].type = PHYS_SHAPE_SPHERE;
        colliders[i].shape_index = (uint32_t)i;
    }
    phys_sphere_t spheres[4] = {{1.0f}, {1.0f}, {1.0f}, {1.0f}};
    phys_collision_pair_t pairs[2] = {{0, 1}, {2, 3}};

    phys_manifold_t manifolds[1];
    memset(manifolds, 0, sizeof(manifolds));
    uint32_t manifold_count = 0;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 64 * 1024);

    phys_stage_ccd_dynamic(&(phys_ccd_dynamic_args_t){
        .bodies_prev = prev,
        .bodies_curr = curr,
        .colliders   = colliders,
        .spheres     = spheres,
        .capsules    = NULL,
        .boxes       = NULL,
        .pairs       = pairs,
        .pair_count  = 2,
        .body_count  = 4,
        .manifolds_out      = manifolds,
        .manifold_count_out = &manifold_count,
        .max_manifolds      = 1,
        .arena       = &arena,
        .dt          = 1.0f / 60.0f,
    });

    /* Should cap at 1. */
    ASSERT_EQ(manifold_count, 1);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/* ── Test: null args safety ────────────────────────────────────── */

static int test_null_args_noop(void) {
    /* Should not crash. */
    phys_stage_ccd_dynamic(NULL);
    return 0;
}

/* ── main ──────────────────────────────────────────────────────── */

int main(void) {
    printf("=== p121_ccd_dynamic_tests ===\n");

    RUN(test_sphere_vs_sphere_headon);
    RUN(test_sphere_parallel_no_collision);
    RUN(test_capsule_vs_box_tunneling);
    RUN(test_already_overlapping);
    RUN(test_skips_static_body);
    RUN(test_skips_mesh_shapes);
    RUN(test_requires_ccd_flag);
    RUN(test_rotation_sweep);
    RUN(test_manifold_body_indices);
    RUN(test_max_manifolds_cap);
    RUN(test_null_args_noop);

    printf("\n  Results: %d passed, %d failed\n\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
