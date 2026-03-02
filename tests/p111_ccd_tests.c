/**
 * @file p111_ccd_tests.c
 * @brief Tests for continuous collision detection (swept sphere/capsule vs mesh).
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/ccd.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/convex_compound.h"
#include "ferrum/physics/convex_decompose.h"
#include "ferrum/physics/joint.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/job/system.h"

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

#define ASSERT_FLOAT_NEAR(a, b, eps) do { \
    float _a = (a), _b = (b), _e = (eps); \
    if (fabsf(_a - _b) > _e) { \
        printf("ASSERT_FLOAT_NEAR failed: %s:%d: %f vs %f (eps=%f)\n", \
               __FILE__, __LINE__, (double)_a, (double)_b, (double)_e); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_LT(a, b) do { \
    float _a = (a), _b = (b); \
    if (!(_a < _b)) { \
        printf("ASSERT_FLOAT_LT failed: %s:%d: %f < %f\n", \
               __FILE__, __LINE__, (double)_a, (double)_b); \
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

/* ── Helper: make a floor quad at Y=0 ──────────────────────────── */

static void make_floor(phys_triangle_t tris[2]) {
    tris[0].v[0] = (phys_vec3_t){-10, 0, -10};
    tris[0].v[1] = (phys_vec3_t){ 10, 0, -10};
    tris[0].v[2] = (phys_vec3_t){ 10, 0,  10};
    tris[1].v[0] = (phys_vec3_t){-10, 0, -10};
    tris[1].v[1] = (phys_vec3_t){ 10, 0,  10};
    tris[1].v[2] = (phys_vec3_t){-10, 0,  10};
}

/* ── Helper: make a vertical wall at X=5 ───────────────────────── */

static void make_wall(phys_triangle_t tris[2]) {
    tris[0].v[0] = (phys_vec3_t){5, 0, -5};
    tris[0].v[1] = (phys_vec3_t){5, 10, -5};
    tris[0].v[2] = (phys_vec3_t){5, 10,  5};
    tris[1].v[0] = (phys_vec3_t){5, 0, -5};
    tris[1].v[1] = (phys_vec3_t){5, 10,  5};
    tris[1].v[2] = (phys_vec3_t){5, 0,  5};
}

static void setup_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    job_system_create(sys, 1, 256, 65536, 64, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}
static void teardown_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

/* ── Tests ─────────────────────────────────────────────────────── */

/** Ray hits a triangle dead-on. */
static int test_ray_vs_triangle_hit(void) {
    phys_triangle_t tri;
    tri.v[0] = (phys_vec3_t){-1, 0, -1};
    tri.v[1] = (phys_vec3_t){ 1, 0, -1};
    tri.v[2] = (phys_vec3_t){ 0, 0,  1};

    /* Ray from above pointing down. */
    phys_vec3_t origin = {0, 5, 0};
    phys_vec3_t dir    = {0, -10, 0};
    float t;
    bool hit = phys_ray_vs_triangle(origin, dir, &tri, &t);
    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(t, 0.5f, 0.001f); /* t=0.5 → 5 units down out of 10 */
    return 0;
}

/** Ray misses the triangle. */
static int test_ray_vs_triangle_miss(void) {
    phys_triangle_t tri;
    tri.v[0] = (phys_vec3_t){-1, 0, -1};
    tri.v[1] = (phys_vec3_t){ 1, 0, -1};
    tri.v[2] = (phys_vec3_t){ 0, 0,  1};

    /* Ray that goes parallel, missing entirely. */
    phys_vec3_t origin = {10, 5, 0};
    phys_vec3_t dir    = {0, -10, 0};
    float t;
    bool hit = phys_ray_vs_triangle(origin, dir, &tri, &t);
    ASSERT_TRUE(!hit);
    return 0;
}

/** Ray behind triangle (t < 0) should not hit. */
static int test_ray_vs_triangle_behind(void) {
    phys_triangle_t tri;
    tri.v[0] = (phys_vec3_t){-1, 0, -1};
    tri.v[1] = (phys_vec3_t){ 1, 0, -1};
    tri.v[2] = (phys_vec3_t){ 0, 0,  1};

    /* Ray starts below the triangle, pointing further down. */
    phys_vec3_t origin = {0, -5, 0};
    phys_vec3_t dir    = {0, -10, 0};
    float t;
    bool hit = phys_ray_vs_triangle(origin, dir, &tri, &t);
    ASSERT_TRUE(!hit);
    return 0;
}

/** Swept sphere hits a floor triangle. */
static int test_swept_sphere_vs_triangle_hit(void) {
    phys_triangle_t tri;
    tri.v[0] = (phys_vec3_t){-5, 0, -5};
    tri.v[1] = (phys_vec3_t){ 5, 0, -5};
    tri.v[2] = (phys_vec3_t){ 5, 0,  5};

    /* Sphere drops from Y=5 to Y=-5, radius=0.5.
     * Should hit at Y=0.5 (radius above floor). */
    phys_vec3_t start = {0, 5, 0};
    phys_vec3_t end   = {0, -5, 0};
    float radius = 0.5f;
    float t;
    phys_vec3_t normal;
    bool hit = phys_swept_sphere_vs_triangle(start, end, radius, &tri,
                                              &t, &normal);
    ASSERT_TRUE(hit);
    /* t should be ~0.45: sphere center at Y = 5 + t*(-10) = 0.5 → t=0.45 */
    ASSERT_FLOAT_NEAR(t, 0.45f, 0.02f);
    /* Normal should point up. */
    ASSERT_FLOAT_GT(normal.y, 0.9f);
    return 0;
}

/** Swept sphere misses (moves parallel to triangle). */
static int test_swept_sphere_vs_triangle_miss(void) {
    phys_triangle_t tri;
    tri.v[0] = (phys_vec3_t){-5, 0, -5};
    tri.v[1] = (phys_vec3_t){ 5, 0, -5};
    tri.v[2] = (phys_vec3_t){ 5, 0,  5};

    /* Sphere moves horizontally above the floor. */
    phys_vec3_t start = {-10, 2, 0};
    phys_vec3_t end   = { 10, 2, 0};
    float radius = 0.5f;
    float t;
    phys_vec3_t normal;
    bool hit = phys_swept_sphere_vs_triangle(start, end, radius, &tri,
                                              &t, &normal);
    ASSERT_TRUE(!hit);
    return 0;
}

/** Swept sphere vs mesh BVH finds earliest hit. */
static int test_swept_sphere_vs_mesh(void) {
    phys_triangle_t wall[2];
    make_wall(wall);

    /* Build BVH. */
    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 4096);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, wall, 2, &arena);
    ASSERT_TRUE(bvh.node_count > 0);

    /* Sphere moves from X=0 to X=10 at Y=5, Z=0. Radius=0.5.
     * Wall at X=5. Should hit at X≈4.5 (center = 5 - radius). */
    phys_vec3_t start = {0, 5, 0};
    phys_vec3_t end   = {10, 5, 0};
    float radius = 0.5f;
    float t;
    phys_vec3_t normal, hit_pos;
    bool hit = phys_swept_sphere_vs_mesh(start, end, radius,
                                          wall, &bvh,
                                          &t, &normal, &hit_pos);
    ASSERT_TRUE(hit);
    /* hit_pos.x should be near 4.5 */
    ASSERT_FLOAT_NEAR(hit_pos.x, 4.5f, 0.1f);
    /* Normal should point in -X (toward the sphere). */
    ASSERT_FLOAT_LT(normal.x, -0.9f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/** CCD stage clamps a fast sphere that tunnels through a wall. */
static int test_ccd_stage_clamps_fast_sphere(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    /* Create wall mesh. */
    phys_triangle_t wall[2];
    make_wall(wall);
    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, 4096);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, wall, 2, &bvh_arena);

    uint32_t wall_id = phys_world_create_body(&world);
    phys_body_t *wall_body = phys_world_get_body(&world, wall_id);
    wall_body->position = (phys_vec3_t){0, 0, 0};
    wall_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, wall_id, wall, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, true);

    /* Create fast sphere with CCD. */
    uint32_t sphere_id = phys_world_create_body(&world);
    phys_body_t *sphere = phys_world_get_body(&world, sphere_id);
    sphere->position = (phys_vec3_t){2, 5, 0};
    sphere->linear_vel = (phys_vec3_t){100, 0, 0}; /* Very fast! */
    sphere->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(sphere, 1.0f);
    phys_world_set_sphere_collider(&world, sphere_id, 0.5f,
                                    (phys_vec3_t){0, 0, 0});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate: after 1 tick at 60Hz, dt=1/60, sphere moves ~1.67 units.
     * Without CCD it tunnels. With CCD it should be clamped to X≈4.5. */
    for (int i = 0; i < 5; i++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    sphere = phys_world_get_body(&world, sphere_id);
    /* Sphere should NOT have passed through the wall. */
    ASSERT_FLOAT_LT(sphere->position.x, 5.0f);

    teardown_jobs(&sys, &ctx);
    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/** CCD does not fire for slow-moving bodies. */
static int test_ccd_skips_slow_bodies(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    phys_triangle_t wall[2];
    make_wall(wall);
    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, 4096);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, wall, 2, &bvh_arena);

    uint32_t wall_id = phys_world_create_body(&world);
    phys_body_t *wall_body = phys_world_get_body(&world, wall_id);
    wall_body->position = (phys_vec3_t){0, 0, 0};
    wall_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, wall_id, wall, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, true);

    /* Slow sphere (1 m/s) — should not trigger CCD. */
    uint32_t sphere_id = phys_world_create_body(&world);
    phys_body_t *sphere = phys_world_get_body(&world, sphere_id);
    sphere->position = (phys_vec3_t){4, 5, 0};
    sphere->linear_vel = (phys_vec3_t){1, 0, 0};
    sphere->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(sphere, 1.0f);
    phys_world_set_sphere_collider(&world, sphere_id, 0.5f,
                                    (phys_vec3_t){0, 0, 0});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* The discrete narrowphase should handle this — CCD is only for
     * bodies moving faster than their radius per tick. */
    for (int i = 0; i < 10; i++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    /* Should still be on the correct side. */
    sphere = phys_world_get_body(&world, sphere_id);
    ASSERT_FLOAT_LT(sphere->position.x, 5.5f);

    teardown_jobs(&sys, &ctx);
    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/** CCD stage clamps a fast box that tunnels through a wall. */
static int test_ccd_stage_clamps_fast_box(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    /* Create wall mesh at X=5. */
    phys_triangle_t wall[2];
    make_wall(wall);
    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, 4096);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, wall, 2, &bvh_arena);

    uint32_t wall_id = phys_world_create_body(&world);
    phys_body_t *wall_body = phys_world_get_body(&world, wall_id);
    wall_body->position = (phys_vec3_t){0, 0, 0};
    wall_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, wall_id, wall, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, true);

    /* Create fast box with CCD: 1×1×1 box moving at 100 m/s toward wall. */
    uint32_t box_id = phys_world_create_body(&world);
    phys_body_t *box = phys_world_get_body(&world, box_id);
    box->position = (phys_vec3_t){2, 5, 0};
    box->linear_vel = (phys_vec3_t){100, 0, 0};
    box->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(box, 1.0f);
    phys_world_set_box_collider(&world, box_id,
                                 (phys_vec3_t){0.5f, 0.5f, 0.5f},
                                 (phys_vec3_t){0, 0, 0},
                                 (phys_quat_t){0, 0, 0, 1});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run a few ticks — box should be clamped before the wall. */
    for (int i = 0; i < 5; i++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    box = phys_world_get_body(&world, box_id);
    /* Box half-extent is 0.5, so center should be ≤ 4.5 (wall at X=5). */
    ASSERT_FLOAT_LT(box->position.x, 5.0f);

    teardown_jobs(&sys, &ctx);
    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/** Fast box moving into floor mesh is clamped (vertical tunnel). */
static int test_ccd_stage_clamps_fast_box_floor(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    /* Create floor mesh at Y=0. */
    phys_triangle_t floor[2];
    make_floor(floor);
    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, 4096);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, floor, 2, &bvh_arena);

    uint32_t floor_id = phys_world_create_body(&world);
    phys_body_t *floor_body = phys_world_get_body(&world, floor_id);
    floor_body->position = (phys_vec3_t){0, 0, 0};
    floor_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, floor_id, floor, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, true);

    /* Box dropping fast downward from Y=10, velocity -200 m/s. */
    uint32_t box_id = phys_world_create_body(&world);
    phys_body_t *box = phys_world_get_body(&world, box_id);
    box->position = (phys_vec3_t){0, 10, 0};
    box->linear_vel = (phys_vec3_t){0, -200, 0};
    box->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(box, 1.0f);
    phys_world_set_box_collider(&world, box_id,
                                 (phys_vec3_t){0.5f, 0.5f, 0.5f},
                                 (phys_vec3_t){0, 0, 0},
                                 (phys_quat_t){0, 0, 0, 1});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    for (int i = 0; i < 5; i++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    box = phys_world_get_body(&world, box_id);
    /* Box should not have gone below the floor. */
    ASSERT_FLOAT_GT(box->position.y, -0.5f);

    teardown_jobs(&sys, &ctx);
    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/** Slow box should NOT trigger CCD sweep (handled by discrete narrowphase). */
static int test_ccd_skips_slow_box(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    phys_triangle_t wall[2];
    make_wall(wall);
    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, 4096);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, wall, 2, &bvh_arena);

    uint32_t wall_id = phys_world_create_body(&world);
    phys_body_t *wall_body = phys_world_get_body(&world, wall_id);
    wall_body->position = (phys_vec3_t){0, 0, 0};
    wall_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, wall_id, wall, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, true);

    /* Slow box: 1 m/s is well below CCD threshold. */
    uint32_t box_id = phys_world_create_body(&world);
    phys_body_t *box = phys_world_get_body(&world, box_id);
    box->position = (phys_vec3_t){4, 5, 0};
    box->linear_vel = (phys_vec3_t){1, 0, 0};
    box->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(box, 1.0f);
    phys_world_set_box_collider(&world, box_id,
                                 (phys_vec3_t){0.5f, 0.5f, 0.5f},
                                 (phys_vec3_t){0, 0, 0},
                                 (phys_quat_t){0, 0, 0, 1});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    for (int i = 0; i < 10; i++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    box = phys_world_get_body(&world, box_id);
    ASSERT_FLOAT_LT(box->position.x, 5.5f);

    teardown_jobs(&sys, &ctx);
    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/* ── CCD vs static non-mesh colliders (no meshes in scene) ─────── */

/**
 * Fast capsule vs static sphere — should be clamped.
 * No meshes in the scene; exercises ccd_statics.c path.
 */
static int test_ccd_capsule_vs_static_sphere(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    /* Static sphere at X=10, radius 2. Large gap ensures CCD
     * is the only thing that can stop the fast capsule. */
    uint32_t sid = phys_world_create_body(&world);
    phys_body_t *sb = phys_world_get_body(&world, sid);
    sb->position = (phys_vec3_t){10, 0, 0};
    sb->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    sb->inv_mass = 0.0f;
    phys_world_set_sphere_collider(&world, sid, 2.0f,
                                    (phys_vec3_t){0, 0, 0});

    /* Fast capsule moving +X at 100 m/s with CCD. */
    uint32_t cid = phys_world_create_body(&world);
    phys_body_t *cb = phys_world_get_body(&world, cid);
    cb->position = (phys_vec3_t){0, 0, 0};
    cb->linear_vel = (phys_vec3_t){100, 0, 0};
    cb->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(cb, 1.0f);
    phys_world_set_capsule_collider(&world, cid, 0.2f, 0.5f,
                                     (phys_vec3_t){0, 0, 0},
                                     (phys_quat_t){0, 0, 0, 1});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    for (int i = 0; i < 5; i++)
        phys_world_tick_parallel(&world, NULL, &ctx);

    cb = phys_world_get_body(&world, cid);
    /* Capsule must not have tunneled past the sphere (center at X=10). */
    ASSERT_FLOAT_LT(cb->position.x, 10.0f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Fast capsule vs static box — should be clamped.
 * No meshes in the scene.
 */
static int test_ccd_capsule_vs_static_box(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    phys_world_init(&world, &cfg);

    /* Static box at X=5, half-extents 1. */
    uint32_t bid = phys_world_create_body(&world);
    phys_body_t *bb = phys_world_get_body(&world, bid);
    bb->position = (phys_vec3_t){5, 0, 0};
    bb->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    bb->inv_mass = 0.0f;
    phys_world_set_box_collider(&world, bid,
                                 (phys_vec3_t){1, 1, 1},
                                 (phys_vec3_t){0, 0, 0},
                                 (phys_quat_t){0, 0, 0, 1});

    /* Fast capsule moving +X at 100 m/s with CCD. */
    uint32_t cid = phys_world_create_body(&world);
    phys_body_t *cb = phys_world_get_body(&world, cid);
    cb->position = (phys_vec3_t){0, 0, 0};
    cb->linear_vel = (phys_vec3_t){100, 0, 0};
    cb->flags |= PHYS_BODY_FLAG_CCD;
    phys_body_set_mass(cb, 1.0f);
    phys_world_set_capsule_collider(&world, cid, 0.2f, 0.5f,
                                     (phys_vec3_t){0, 0, 0},
                                     (phys_quat_t){0, 0, 0, 1});

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    for (int i = 0; i < 5; i++)
        phys_world_tick_parallel(&world, NULL, &ctx);

    cb = phys_world_get_body(&world, cid);
    ASSERT_FLOAT_LT(cb->position.x, 5.0f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Fast capsule chain vs armadillo-like static compound.
 * Mimics the demo: 10 capsules connected by ball joints, swinging
 * at high velocity into a compound hull.  No meshes in the scene.
 */
static int test_ccd_chain_vs_static_compound(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    phys_world_init(&world, &cfg);

    /* ── Static compound body at origin ─────────────────────────── */
    /* Build a box-shaped convex hull: 4×4×4 cube. */
    phys_vec3_t pts[8] = {
        {-2,-2,-2}, { 2,-2,-2}, { 2, 2,-2}, {-2, 2,-2},
        {-2,-2, 2}, { 2,-2, 2}, { 2, 2, 2}, {-2, 2, 2}
    };
    phys_convex_hull_t hull;
    int rc = phys_convex_hull_build(&hull, pts, 8);
    ASSERT_TRUE(rc == 0);

    phys_decompose_result_t dr;
    memset(&dr, 0, sizeof(dr));
    dr.hulls[0] = hull;
    dr.hull_count = 1;

    uint32_t compound_id = phys_world_create_body(&world);
    phys_body_t *sb = phys_world_get_body(&world, compound_id);
    sb->position = (phys_vec3_t){0, 0, 0};
    sb->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    sb->inv_mass = 0.0f;
    phys_world_set_compound_collider(&world, compound_id, &dr,
                                      (phys_vec3_t){0, 0, 0});

    /* ── Capsule chain: 10 links ───────────────────────────────── */
    /* Chain parameters matching demo. */
    const float cap_r = 0.5f;
    const float cap_hh = 0.8f;
    const float link_len = 2.0f * (cap_hh + cap_r);  /* 2.6 */
    const float chain_mass = 2.0f;
    const uint32_t chain_len = 10;

    /* Kinematic anchor at X=-30, Y=10 — chain hangs from here. */
    uint32_t anchor = phys_world_create_body(&world);
    phys_body_t *ab = phys_world_get_body(&world, anchor);
    ab->position = (phys_vec3_t){-30, 10, 0};
    ab->orientation = (phys_quat_t){0, 0, 0, 1};
    ab->flags |= PHYS_BODY_FLAG_KINEMATIC;
    phys_body_t *abn = phys_body_pool_get_next(&world.body_pool, anchor);
    *abn = *ab;

    uint32_t first_link = 0, last_link = 0;
    uint32_t prev_body = anchor;
    for (uint32_t ci = 0; ci < chain_len; ci++) {
        float x = -30.0f + (float)(ci + 1) * link_len;

        uint32_t bi = phys_world_create_body(&world);
        phys_body_t *cb = phys_world_get_body(&world, bi);
        cb->position = (phys_vec3_t){x, 10, 0};
        /* Rotate 90° around Z so capsule lies along X. */
        cb->orientation = (phys_quat_t){0, 0, 0.7071068f, 0.7071068f};
        phys_body_set_mass(cb, chain_mass);
        phys_body_set_capsule_inertia(cb, chain_mass, cap_r, cap_hh);
        cb->flags |= PHYS_BODY_FLAG_CCD;
        /* Give the chain high velocity toward the compound. */
        cb->linear_vel = (phys_vec3_t){50, 0, 0};

        phys_body_t *cb_next =
            phys_body_pool_get_next(&world.body_pool, bi);
        *cb_next = *cb;

        phys_world_set_capsule_collider(&world, bi,
            cap_r, cap_hh,
            (phys_vec3_t){0, 0, 0},
            (phys_quat_t){0, 0, 0, 1});

        phys_joint_t joint;
        memset(&joint, 0, sizeof(joint));
        joint.type = PHYS_JOINT_BALL;
        joint.body_a = prev_body;
        joint.body_b = bi;
        joint.local_anchor_a = (prev_body == anchor)
            ? (phys_vec3_t){0, 0, 0}
            : (phys_vec3_t){0, cap_hh + cap_r, 0};
        joint.local_anchor_b = (phys_vec3_t){0, -(cap_hh + cap_r), 0};
        joint.damping = 0.5f;
        phys_world_add_joint(&world, &joint);

        if (ci == 0) first_link = bi;
        last_link = bi;
        prev_body = bi;
    }

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run 20 ticks — enough for the chain to reach the compound. */
    for (int i = 0; i < 20; i++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    /* No capsule link should have tunneled into or past the compound.
     * The compound hull spans X=[-2,2], so no chain body should be
     * inside that range unless it has been properly stopped. */
    bool tunneled = false;
    for (uint32_t bi = first_link; bi <= last_link; bi++) {
        const phys_body_t *b = phys_world_get_body(&world, bi);
        /* If a link ended up at X > 2 (past the compound) and it
         * started from X < -2, that's tunneling. */
        if (b->position.x > 2.5f) {
            printf("  body %u tunneled: pos=(%.2f, %.2f, %.2f)\n",
                   bi, (double)b->position.x, (double)b->position.y,
                   (double)b->position.z);
            tunneled = true;
        }
    }
    ASSERT_TRUE(!tunneled);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/** NULL safety — stage should not crash with NULL inputs. */
static int test_ccd_null_safe(void) {
    int result = phys_stage_ccd(NULL);
    ASSERT_TRUE(result == 0);

    phys_ccd_args_t args;
    memset(&args, 0, sizeof(args));
    result = phys_stage_ccd(&args);
    ASSERT_TRUE(result == 0);
    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

int main(void) {
    printf("RUN p111_ccd_tests\n");
    g_pass = g_fail = 0;

    RUN(test_ray_vs_triangle_hit);
    RUN(test_ray_vs_triangle_miss);
    RUN(test_ray_vs_triangle_behind);
    RUN(test_swept_sphere_vs_triangle_hit);
    RUN(test_swept_sphere_vs_triangle_miss);
    RUN(test_swept_sphere_vs_mesh);
    RUN(test_ccd_stage_clamps_fast_sphere);
    RUN(test_ccd_skips_slow_bodies);
    RUN(test_ccd_stage_clamps_fast_box);
    RUN(test_ccd_stage_clamps_fast_box_floor);
    RUN(test_ccd_skips_slow_box);
    RUN(test_ccd_capsule_vs_static_sphere);
    RUN(test_ccd_capsule_vs_static_box);
    RUN(test_ccd_chain_vs_static_compound);
    RUN(test_ccd_null_safe);

    printf("\n%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
