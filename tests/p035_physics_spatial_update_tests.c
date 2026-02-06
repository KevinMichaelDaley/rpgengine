/**
 * @file p035_physics_spatial_update_tests.c
 * @brief Unit tests for Stage 2: Spatial Index Update (phys-103).
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/physics/spatial_update.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/phys_pool.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                               \
    do {                                                                       \
        if ((exp) != (act)) {                                                  \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u "      \
                    "got %u\n", __FILE__, __LINE__,                            \
                    (unsigned)(exp), (unsigned)(act));                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                       \
        if (fabsf((float)(exp) - (float)(act)) > (eps)) {                      \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f\n", __FILE__, __LINE__,            \
                    (double)(exp), (double)(act));                              \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                        \
    do {                                                                       \
        ASSERT_FLOAT_NEAR((exp).x, (act).x, (eps));                            \
        ASSERT_FLOAT_NEAR((exp).y, (act).y, (eps));                            \
        ASSERT_FLOAT_NEAR((exp).z, (act).z, (eps));                            \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static bool results_contain(const uint32_t *results, uint32_t count,
                            uint32_t value) {
    for (uint32_t i = 0; i < count; ++i) {
        if (results[i] == value) return true;
    }
    return false;
}

static const phys_vec3_t ZERO_VEC = {0, 0, 0};
static const phys_quat_t IDENTITY_QUAT = {0, 0, 0, 1};

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * test_spatial_update_spheres:
 * 3 spheres at different positions. Verify AABBs computed correctly.
 */
static int test_spatial_update_spheres(void) {
    phys_body_t bodies[3];
    phys_collider_t colliders[3];
    phys_sphere_t spheres[3] = {{.radius = 1.0f},
                                 {.radius = 0.5f},
                                 {.radius = 2.0f}};
    phys_aabb_t aabbs[3];

    for (int i = 0; i < 3; ++i) {
        phys_body_init(&bodies[i]);
        bodies[i].position = (phys_vec3_t){(float)(i * 10), 0, 0};
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, ZERO_VEC);
    }

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &arena);

    phys_spatial_update_args_t args = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .grid_out   = &grid,
        .active     = NULL,
        .body_count = 3
    };

    phys_stage_spatial_update(&args);

    /* Body 0: sphere r=1 at (0,0,0) → AABB [-1,-1,-1] to [1,1,1] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1, -1, -1}), aabbs[0].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1,  1,  1}), aabbs[0].max, 0.001f);

    /* Body 1: sphere r=0.5 at (10,0,0) → AABB [9.5,-0.5,-0.5] to [10.5,0.5,0.5] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){9.5f, -0.5f, -0.5f}), aabbs[1].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){10.5f, 0.5f,  0.5f}), aabbs[1].max, 0.001f);

    /* Body 2: sphere r=2 at (20,0,0) → AABB [18,-2,-2] to [22,2,2] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){18, -2, -2}), aabbs[2].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){22,  2,  2}), aabbs[2].max, 0.001f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * test_spatial_update_grid_populated:
 * After update, grid query finds bodies at their locations.
 */
static int test_spatial_update_grid_populated(void) {
    phys_body_t bodies[3];
    phys_collider_t colliders[3];
    phys_sphere_t spheres[3] = {{.radius = 1.0f},
                                 {.radius = 1.0f},
                                 {.radius = 1.0f}};
    phys_aabb_t aabbs[3];

    for (int i = 0; i < 3; ++i) {
        phys_body_init(&bodies[i]);
        bodies[i].position = (phys_vec3_t){(float)(i * 20), 0, 0};
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, ZERO_VEC);
    }

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &arena);

    phys_spatial_update_args_t args = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .grid_out   = &grid,
        .active     = NULL,
        .body_count = 3
    };

    phys_stage_spatial_update(&args);

    /* Query around body 0's position — should find body 0. */
    uint32_t results[10];
    uint32_t count = phys_spatial_grid_query(&grid, &aabbs[0], results, 10);
    ASSERT_TRUE(count >= 1);
    ASSERT_TRUE(results_contain(results, count, 0));

    /* Query around body 2's position — should find body 2. */
    count = phys_spatial_grid_query(&grid, &aabbs[2], results, 10);
    ASSERT_TRUE(count >= 1);
    ASSERT_TRUE(results_contain(results, count, 2));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * test_spatial_update_box:
 * 1 box body. Verify AABB is correct for an axis-aligned box.
 */
static int test_spatial_update_box(void) {
    phys_body_t body;
    phys_body_init(&body);
    body.position = (phys_vec3_t){5, 5, 5};

    phys_collider_t collider;
    phys_collider_init_box(&collider, 0, ZERO_VEC, IDENTITY_QUAT);

    phys_box_t box = {.half_extents = {2, 3, 4}};
    phys_aabb_t aabb;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &arena);

    phys_spatial_update_args_t args = {
        .bodies     = &body,
        .colliders  = &collider,
        .spheres    = NULL,
        .boxes      = &box,
        .capsules   = NULL,
        .aabbs_out  = &aabb,
        .grid_out   = &grid,
        .active     = NULL,
        .body_count = 1
    };

    phys_stage_spatial_update(&args);

    /* Box at (5,5,5) with half_extents (2,3,4), identity rotation
     * → AABB [3,2,1] to [7,8,9] */
    ASSERT_VEC3_NEAR(((phys_vec3_t){3, 2, 1}), aabb.min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){7, 8, 9}), aabb.max, 0.001f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * test_spatial_update_capsule:
 * 1 capsule body. Verify AABB is correct for an upright capsule.
 */
static int test_spatial_update_capsule(void) {
    phys_body_t body;
    phys_body_init(&body);
    body.position = (phys_vec3_t){0, 0, 0};

    phys_collider_t collider;
    phys_collider_init_capsule(&collider, 0, ZERO_VEC, IDENTITY_QUAT);

    /* Capsule: radius=1, half_height=2, aligned along +Y.
     * Total height = 2*half_height + 2*radius = 6.
     * AABB min = (-1, -3, -1), max = (1, 3, 1). */
    phys_capsule_t capsule = {.radius = 1.0f, .half_height = 2.0f};
    phys_aabb_t aabb;

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &arena);

    phys_spatial_update_args_t args = {
        .bodies     = &body,
        .colliders  = &collider,
        .spheres    = NULL,
        .boxes      = NULL,
        .capsules   = &capsule,
        .aabbs_out  = &aabb,
        .grid_out   = &grid,
        .active     = NULL,
        .body_count = 1
    };

    phys_stage_spatial_update(&args);

    /* Capsule along +Y: endpoints at (0, ±2, 0), each with radius 1.
     * AABB = [-1, -3, -1] to [1, 3, 1]. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1, -3, -1}), aabb.min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1,  3,  1}), aabb.max, 0.001f);

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * test_spatial_update_skips_inactive:
 * Body at slot 1 is inactive → its AABB should not be computed,
 * and it should not appear in grid queries.
 */
static int test_spatial_update_skips_inactive(void) {
    phys_body_t bodies[3];
    phys_collider_t colliders[3];
    phys_sphere_t spheres[3] = {{.radius = 1.0f},
                                 {.radius = 1.0f},
                                 {.radius = 1.0f}};
    phys_aabb_t aabbs[3];

    for (int i = 0; i < 3; ++i) {
        phys_body_init(&bodies[i]);
        bodies[i].position = (phys_vec3_t){(float)(i * 10), 0, 0};
        phys_collider_init_sphere(&colliders[i], (uint32_t)i, ZERO_VEC);
    }

    /* Mark body 1 as inactive. */
    uint8_t active[3] = {1, 0, 1};

    /* Pre-fill aabbs[1] with sentinel values to verify it's untouched. */
    aabbs[1].min = (phys_vec3_t){999, 999, 999};
    aabbs[1].max = (phys_vec3_t){999, 999, 999};

    phys_frame_arena_t arena;
    phys_frame_arena_init(&arena, 1024 * 1024);
    phys_spatial_grid_t grid;
    phys_spatial_grid_init(&grid, 256, 10.0f, &arena);

    phys_spatial_update_args_t args = {
        .bodies     = bodies,
        .colliders  = colliders,
        .spheres    = spheres,
        .boxes      = NULL,
        .capsules   = NULL,
        .aabbs_out  = aabbs,
        .grid_out   = &grid,
        .active     = active,
        .body_count = 3
    };

    phys_stage_spatial_update(&args);

    /* Body 0 should be processed normally. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){-1, -1, -1}), aabbs[0].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){ 1,  1,  1}), aabbs[0].max, 0.001f);

    /* Body 1 AABB should remain at sentinel values (untouched). */
    ASSERT_FLOAT_NEAR(999.0f, aabbs[1].min.x, 0.001f);
    ASSERT_FLOAT_NEAR(999.0f, aabbs[1].max.x, 0.001f);

    /* Body 2 should be processed normally. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){19, -1, -1}), aabbs[2].min, 0.001f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){21,  1,  1}), aabbs[2].max, 0.001f);

    /* Grid query around body 1 should NOT find body 1. */
    phys_aabb_t query = {{9, -1, -1}, {11, 1, 1}};
    uint32_t results[10];
    uint32_t count = phys_spatial_grid_query(&grid, &query, results, 10);
    ASSERT_TRUE(!results_contain(results, count, 1));

    phys_frame_arena_destroy(&arena);
    return 0;
}

/**
 * test_spatial_update_null_safe:
 * Calling with NULL args should not crash.
 */
static int test_spatial_update_null_safe(void) {
    phys_stage_spatial_update(NULL);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"spatial_update_spheres",        test_spatial_update_spheres},
    {"spatial_update_grid_populated", test_spatial_update_grid_populated},
    {"spatial_update_box",            test_spatial_update_box},
    {"spatial_update_capsule",        test_spatial_update_capsule},
    {"spatial_update_skips_inactive", test_spatial_update_skips_inactive},
    {"spatial_update_null_safe",      test_spatial_update_null_safe},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
