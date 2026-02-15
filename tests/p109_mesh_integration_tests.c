/**
 * @file p109_mesh_integration_tests.c
 * @brief Integration tests for Phase 9: Mesh collider in full physics world.
 *
 * Tests:
 *   - Sphere falls onto a triangulated floor and comes to rest
 *   - Box slides down a triangulated ramp under gravity
 *   - No tunneling through thin mesh walls
 *   - NULL safety for mesh collider setter
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/mesh_collider.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/world.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_GT(a, b)                                                 \
    do {                                                                       \
        float _a = (a), _b = (b);                                             \
        if (!(_a > _b)) {                                                      \
            fprintf(stderr, "ASSERT_FLOAT_GT failed: %s:%d: %.6f > %.6f\n",   \
                    __FILE__, __LINE__, (double)_a, (double)_b);              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_LT(a, b)                                                 \
    do {                                                                       \
        float _a = (a), _b = (b);                                             \
        if (!(_a < _b)) {                                                      \
            fprintf(stderr, "ASSERT_FLOAT_LT failed: %s:%d: %.6f < %.6f\n",   \
                    __FILE__, __LINE__, (double)_a, (double)_b);              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                      \
    do {                                                                       \
        float _e = (exp), _a = (act), _t = (tol);                             \
        if (fabsf(_e - _a) > _t) {                                            \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "              \
                    "expected %.6f got %.6f (tol=%.6f)\n",                     \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);  \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define RUN_TEST(fn)                                                          \
    do {                                                                       \
        printf("  %-60s", #fn);                                                \
        int _r = fn();                                                         \
        if (_r) { printf("[FAIL]\n"); fail_count++; }                          \
        else    { printf("[OK]\n"); }                                          \
        test_count++;                                                          \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

#define ARENA_SIZE (4u * 1024u * 1024u)

static const phys_quat_t identity_quat = {0, 0, 0, 1};

static int make_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16u;
    cfg.max_colliders = 16u;
    cfg.manifold_cache_size = 64u;
    cfg.frame_arena_size = 8u * 1024u * 1024u;
    return phys_world_init(world, &cfg);
}

/** Build two large triangles forming a flat floor at Y=0. */
static void make_floor_mesh(phys_triangle_t tris[2]) {
    /* CCW winding when viewed from above → normal points up (+Y). */
    tris[0].v[0] = (phys_vec3_t){-50, 0, -50};
    tris[0].v[1] = (phys_vec3_t){ 50, 0,  50};
    tris[0].v[2] = (phys_vec3_t){ 50, 0, -50};
    tris[1].v[0] = (phys_vec3_t){-50, 0, -50};
    tris[1].v[1] = (phys_vec3_t){-50, 0,  50};
    tris[1].v[2] = (phys_vec3_t){ 50, 0,  50};
}

/**
 * Build a triangulated ramp surface.
 * The ramp goes from (0,5,0) at the top down to (10,0,0) at the bottom,
 * with 5 units of width along Z axis.
 */
static void make_ramp_mesh(phys_triangle_t tris[2]) {
    tris[0].v[0] = (phys_vec3_t){ 0, 5, -5};
    tris[0].v[1] = (phys_vec3_t){10, 0, -5};
    tris[0].v[2] = (phys_vec3_t){10, 0,  5};
    tris[1].v[0] = (phys_vec3_t){ 0, 5, -5};
    tris[1].v[1] = (phys_vec3_t){10, 0,  5};
    tris[1].v[2] = (phys_vec3_t){ 0, 5,  5};
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Sphere dropped onto a mesh floor.
 * After many ticks, the sphere should come to rest near Y=radius.
 */
static int test_sphere_on_terrain_mesh(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world) == 0);

    /* BVH arena for mesh — separate from world's frame arena. */
    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, ARENA_SIZE);

    /* Create static mesh floor body. */
    phys_triangle_t floor_tris[2];
    make_floor_mesh(floor_tris);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, floor_tris, 2, &bvh_arena);

    uint32_t floor_id = phys_world_create_body(&world);
    ASSERT_TRUE(floor_id != UINT32_MAX);
    phys_body_t *floor_body = phys_world_get_body(&world, floor_id);
    floor_body->position = (phys_vec3_t){0, 0, 0};
    floor_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, floor_id, floor_tris, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, false);

    /* Create dynamic sphere above the floor. */
    uint32_t sphere_id = phys_world_create_body(&world);
    ASSERT_TRUE(sphere_id != UINT32_MAX);
    phys_body_t *sphere_body = phys_world_get_body(&world, sphere_id);
    sphere_body->position = (phys_vec3_t){0, 5, 0};
    phys_body_set_mass(sphere_body, 1.0f);
    phys_world_set_sphere_collider(&world, sphere_id, 0.5f,
                                    (phys_vec3_t){0, 0, 0});

    /* Step the simulation for 300 ticks (~5 seconds at 60Hz). */
    for (int i = 0; i < 300; i++) {
        phys_world_tick(&world, NULL);
    }

    /* Sphere should have fallen and settled near Y ≈ 0.5 (radius). */
    sphere_body = phys_world_get_body(&world, sphere_id);
    float final_y = sphere_body->position.y;

    /* Should be above the floor (not tunneled through). */
    ASSERT_FLOAT_GT(final_y, -0.1f);
    /* Should have fallen from Y=5 — not still up there. */
    ASSERT_FLOAT_LT(final_y, 2.0f);

    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Box placed on a ramp mesh.
 * After ticks, the box should have slid down (X increased, Y decreased).
 */
static int test_box_slides_down_ramp(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world) == 0);

    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, ARENA_SIZE);

    /* Create static ramp mesh. */
    phys_triangle_t ramp_tris[2];
    make_ramp_mesh(ramp_tris);
    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, ramp_tris, 2, &bvh_arena);

    uint32_t ramp_id = phys_world_create_body(&world);
    ASSERT_TRUE(ramp_id != UINT32_MAX);
    phys_body_t *ramp_body = phys_world_get_body(&world, ramp_id);
    ramp_body->position = (phys_vec3_t){0, 0, 0};
    ramp_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, ramp_id, ramp_tris, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, false);

    /* Create dynamic box near the top of the ramp. */
    uint32_t box_id = phys_world_create_body(&world);
    ASSERT_TRUE(box_id != UINT32_MAX);
    phys_body_t *box_body = phys_world_get_body(&world, box_id);
    box_body->position = (phys_vec3_t){2, 6, 0}; /* Above the ramp top. */
    phys_body_set_mass(box_body, 1.0f);
    phys_world_set_box_collider(&world, box_id,
                                 (phys_vec3_t){0.3f, 0.3f, 0.3f},
                                 (phys_vec3_t){0, 0, 0}, identity_quat);

    float start_y = box_body->position.y;

    /* Step 300 ticks. */
    for (int i = 0; i < 300; i++) {
        phys_world_tick(&world, NULL);
    }

    box_body = phys_world_get_body(&world, box_id);
    float final_y = box_body->position.y;

    /* Box should have fallen/slid — Y decreased. */
    ASSERT_FLOAT_LT(final_y, start_y - 1.0f);
    /* Should not have fallen through infinity. */
    ASSERT_FLOAT_GT(final_y, -5.0f);

    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/**
 * No tunneling: fast sphere should not pass through thin mesh wall.
 * A thin vertical wall made of two triangles, sphere launched at it.
 */
static int test_no_tunneling_through_thin_wall(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world) == 0);

    phys_frame_arena_t bvh_arena;
    phys_frame_arena_init(&bvh_arena, ARENA_SIZE);

    /* Vertical wall at X=5, spanning Z=[-5,5], Y=[0,10]. */
    phys_triangle_t wall_tris[2];
    wall_tris[0].v[0] = (phys_vec3_t){5, 0, -5};
    wall_tris[0].v[1] = (phys_vec3_t){5, 10, -5};
    wall_tris[0].v[2] = (phys_vec3_t){5, 10,  5};
    wall_tris[1].v[0] = (phys_vec3_t){5, 0, -5};
    wall_tris[1].v[1] = (phys_vec3_t){5, 10,  5};
    wall_tris[1].v[2] = (phys_vec3_t){5, 0,  5};

    phys_mesh_bvh_t bvh;
    phys_mesh_bvh_build(&bvh, wall_tris, 2, &bvh_arena);

    uint32_t wall_id = phys_world_create_body(&world);
    ASSERT_TRUE(wall_id != UINT32_MAX);
    phys_body_t *wall_body = phys_world_get_body(&world, wall_id);
    wall_body->position = (phys_vec3_t){0, 0, 0};
    wall_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, wall_id, wall_tris, 2, &bvh,
                                  (phys_vec3_t){0, 0, 0}, false);

    /* Also place a floor so the sphere doesn't just fall forever. */
    phys_triangle_t floor_tris[2];
    make_floor_mesh(floor_tris);
    phys_mesh_bvh_t floor_bvh;
    phys_mesh_bvh_build(&floor_bvh, floor_tris, 2, &bvh_arena);

    uint32_t floor_id = phys_world_create_body(&world);
    ASSERT_TRUE(floor_id != UINT32_MAX);
    phys_body_t *floor_body = phys_world_get_body(&world, floor_id);
    floor_body->position = (phys_vec3_t){0, 0, 0};
    floor_body->flags |= (uint32_t)PHYS_BODY_FLAG_STATIC;
    phys_world_set_mesh_collider(&world, floor_id, floor_tris, 2, &floor_bvh,
                                  (phys_vec3_t){0, 0, 0}, false);

    /* Sphere starting at X=2, heading toward wall at X=5. */
    uint32_t sphere_id = phys_world_create_body(&world);
    ASSERT_TRUE(sphere_id != UINT32_MAX);
    phys_body_t *sphere_body = phys_world_get_body(&world, sphere_id);
    sphere_body->position = (phys_vec3_t){2, 1, 0};
    phys_body_set_mass(sphere_body, 1.0f);
    sphere_body->linear_vel = (phys_vec3_t){10, 0, 0}; /* Fast toward wall. */
    phys_world_set_sphere_collider(&world, sphere_id, 0.5f,
                                    (phys_vec3_t){0, 0, 0});

    /* Step 120 ticks (~2 seconds). */
    for (int i = 0; i < 120; i++) {
        phys_world_tick(&world, NULL);
    }

    sphere_body = phys_world_get_body(&world, sphere_id);

    /* Sphere should not have passed through the wall (X < 5 + radius). */
    ASSERT_FLOAT_LT(sphere_body->position.x, 6.0f);

    phys_frame_arena_destroy(&bvh_arena);
    phys_world_destroy(&world);
    return 0;
}

/** NULL safety: set_mesh_collider with NULL args should not crash. */
static int test_mesh_collider_null_safe(void) {
    phys_world_t world;
    ASSERT_TRUE(make_world(&world) == 0);

    /* NULL world. */
    phys_world_set_mesh_collider(NULL, 0, NULL, 0, NULL,
                                  (phys_vec3_t){0, 0, 0}, false);

    /* NULL bvh. */
    phys_world_set_mesh_collider(&world, 0, NULL, 0, NULL,
                                  (phys_vec3_t){0, 0, 0}, false);

    /* Out of range body index. */
    phys_mesh_bvh_t bvh;
    memset(&bvh, 0, sizeof(bvh));
    phys_world_set_mesh_collider(&world, 9999, NULL, 0, &bvh,
                                  (phys_vec3_t){0, 0, 0}, false);

    phys_world_destroy(&world);
    return 0;
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("RUN p109_mesh_integration_tests\n");

    int fail_count = 0;
    int test_count = 0;

    RUN_TEST(test_sphere_on_terrain_mesh);
    RUN_TEST(test_box_slides_down_ramp);
    RUN_TEST(test_no_tunneling_through_thin_wall);
    RUN_TEST(test_mesh_collider_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
