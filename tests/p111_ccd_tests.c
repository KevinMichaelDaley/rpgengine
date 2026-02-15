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
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"

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

    /* Simulate: after 1 tick at 60Hz, dt=1/60, sphere moves ~1.67 units.
     * Without CCD it tunnels. With CCD it should be clamped to X≈4.5. */
    for (int i = 0; i < 5; i++) {
        phys_world_tick(&world, NULL);
    }

    sphere = phys_world_get_body(&world, sphere_id);
    /* Sphere should NOT have passed through the wall. */
    ASSERT_FLOAT_LT(sphere->position.x, 5.0f);

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

    /* The discrete narrowphase should handle this — CCD is only for
     * bodies moving faster than their radius per tick. */
    for (int i = 0; i < 10; i++) {
        phys_world_tick(&world, NULL);
    }

    /* Should still be on the correct side. */
    sphere = phys_world_get_body(&world, sphere_id);
    ASSERT_FLOAT_LT(sphere->position.x, 5.5f);

    phys_frame_arena_destroy(&bvh_arena);
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
    RUN(test_ccd_null_safe);

    printf("\n%d/%d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
