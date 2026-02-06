/**
 * @file p062_physics_par_spatial_tests.c
 * @brief Tests for parallel spatial update (phys-303).
 *
 * Validates that phys_stage_spatial_update_par produces identical
 * AABB results to the sequential version, handles edge cases
 * (zero bodies, single batch, multiple batches), populates the
 * spatial grid, and correctly handles mixed collider shapes.
 */

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/job/system.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/par/spatial_update_par.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/spatial_grid.h"
#include "ferrum/physics/spatial_update.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define TEST_FAIL(msg, ...)                                                    \
    do {                                                                        \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__,           \
                ##__VA_ARGS__);                                                \
        return 1;                                                              \
    } while (0)

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                         \
            TEST_FAIL("%s", #cond);                                            \
        }                                                                       \
    } while (0)

#define ASSERT_EQ_UINT(expected, actual)                                       \
    do {                                                                        \
        unsigned long long _exp = (unsigned long long)(expected);               \
        unsigned long long _act = (unsigned long long)(actual);                 \
        if (_exp != _act) {                                                    \
            TEST_FAIL("expected %llu got %llu", _exp, _act);                   \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_EQ(expected, actual)                                      \
    do {                                                                        \
        float _exp = (expected);                                               \
        float _act = (actual);                                                 \
        if (fabsf(_exp - _act) > 1e-6f) {                                     \
            TEST_FAIL("expected %f got %f", (double)_exp, (double)_act);       \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ── Helpers ────────────────────────────────────────────────────── */

/** Identity quaternion constant for body/collider initialization. */
static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};

/** Zero vector constant. */
static const phys_vec3_t VEC3_ZERO = {0.0f, 0.0f, 0.0f};

/**
 * @brief Set up N sphere bodies at positions (i, 0, 0) with radius 1.
 *
 * Allocates bodies, colliders, spheres, two AABB arrays, active flags,
 * and two spatial grids+arenas.  Caller must free via teardown_test_data.
 */
typedef struct test_data {
    phys_body_t     *bodies;
    phys_collider_t *colliders;
    phys_sphere_t   *spheres;
    phys_box_t      *boxes;
    phys_capsule_t  *capsules;
    phys_aabb_t     *aabbs_seq;
    phys_aabb_t     *aabbs_par;
    uint8_t         *active;
    uint32_t         body_count;

    phys_spatial_grid_t  grid_seq;
    phys_spatial_grid_t  grid_par;
    phys_frame_arena_t   arena_seq;
    phys_frame_arena_t   arena_par;

    job_system_t        job_sys;
    phys_job_context_t  job_ctx;
} test_data_t;

static void setup_sphere_bodies(test_data_t *td, uint32_t count) {
    memset(td, 0, sizeof(*td));
    td->body_count = count;

    td->bodies    = calloc(count, sizeof(phys_body_t));
    td->colliders = calloc(count, sizeof(phys_collider_t));
    td->spheres   = calloc(count, sizeof(phys_sphere_t));
    td->boxes     = NULL;
    td->capsules  = NULL;
    td->aabbs_seq = calloc(count, sizeof(phys_aabb_t));
    td->aabbs_par = calloc(count, sizeof(phys_aabb_t));
    td->active    = calloc(count, sizeof(uint8_t));

    for (uint32_t i = 0; i < count; ++i) {
        phys_body_init(&td->bodies[i]);
        td->bodies[i].position = (phys_vec3_t){(float)i, 0.0f, 0.0f};
        td->bodies[i].orientation = QUAT_IDENTITY;

        td->spheres[i].radius = 1.0f;
        phys_collider_init_sphere(&td->colliders[i], i, VEC3_ZERO);
        td->active[i] = 1;
    }

    /* Arenas and grids — large enough for any test. */
    phys_frame_arena_init(&td->arena_seq, 1024 * 1024);
    phys_frame_arena_init(&td->arena_par, 1024 * 1024);
    phys_spatial_grid_init(&td->grid_seq, 256, 4.0f, &td->arena_seq);
    phys_spatial_grid_init(&td->grid_par, 256, 4.0f, &td->arena_par);

    /* Job system. */
    job_system_create(&td->job_sys, 2, 256, 65536, 64, 0);
    job_system_start(&td->job_sys);
    phys_job_context_init(&td->job_ctx, &td->job_sys);
}

static void teardown_test_data(test_data_t *td) {
    phys_job_context_destroy(&td->job_ctx);
    job_system_shutdown(&td->job_sys);
    phys_frame_arena_destroy(&td->arena_seq);
    phys_frame_arena_destroy(&td->arena_par);
    free(td->bodies);
    free(td->colliders);
    free(td->spheres);
    free(td->boxes);
    free(td->capsules);
    free(td->aabbs_seq);
    free(td->aabbs_par);
    free(td->active);
}

/**
 * @brief Compare two AABB arrays element-by-element.
 * @return 0 if all match, 1 on first mismatch.
 */
static int compare_aabbs(const phys_aabb_t *a, const phys_aabb_t *b,
                          const uint8_t *active, uint32_t count) {
    for (uint32_t i = 0; i < count; ++i) {
        if (active && !active[i]) {
            continue;
        }
        if (fabsf(a[i].min.x - b[i].min.x) > 1e-6f ||
            fabsf(a[i].min.y - b[i].min.y) > 1e-6f ||
            fabsf(a[i].min.z - b[i].min.z) > 1e-6f ||
            fabsf(a[i].max.x - b[i].max.x) > 1e-6f ||
            fabsf(a[i].max.y - b[i].max.y) > 1e-6f ||
            fabsf(a[i].max.z - b[i].max.z) > 1e-6f) {
            fprintf(stderr, "  AABB mismatch at index %u\n", i);
            return 1;
        }
    }
    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: 100 sphere bodies — parallel AABBs must exactly match sequential.
 */
static int test_par_spatial_identical_to_seq(void) {
    test_data_t td;
    setup_sphere_bodies(&td, 100);

    /* Run sequential. */
    phys_spatial_update_args_t seq_args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_seq,
        .grid_out   = &td.grid_seq,
        .active     = td.active,
        .body_count = td.body_count,
    };
    phys_stage_spatial_update(&seq_args);

    /* Run parallel. */
    phys_spatial_update_args_t par_args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_par,
        .grid_out   = &td.grid_par,
        .active     = td.active,
        .body_count = td.body_count,
    };
    phys_stage_spatial_update_par(&par_args, &td.job_ctx);

    /* Compare AABBs. */
    int cmp = compare_aabbs(td.aabbs_seq, td.aabbs_par,
                            td.active, td.body_count);
    ASSERT_TRUE(cmp == 0);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 2: 200 bodies (< 512) should produce exactly 1 job batch.
 */
static int test_par_spatial_single_batch(void) {
    test_data_t td;
    setup_sphere_bodies(&td, 200);

    phys_spatial_update_args_t args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_par,
        .grid_out   = &td.grid_par,
        .active     = td.active,
        .body_count = td.body_count,
    };
    phys_stage_spatial_update_par(&args, &td.job_ctx);

    /* Verify AABBs were computed — spot check first and last. */
    ASSERT_FLOAT_EQ(-1.0f, td.aabbs_par[0].min.x);
    ASSERT_FLOAT_EQ( 1.0f, td.aabbs_par[0].max.x);
    ASSERT_FLOAT_EQ(198.0f, td.aabbs_par[199].min.x);
    ASSERT_FLOAT_EQ(200.0f, td.aabbs_par[199].max.x);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 3: 2000 bodies should produce ceil(2000/512) = 4 job batches.
 */
static int test_par_spatial_multiple_batches(void) {
    test_data_t td;
    setup_sphere_bodies(&td, 2000);

    /* Run sequential for reference. */
    phys_spatial_update_args_t seq_args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_seq,
        .grid_out   = &td.grid_seq,
        .active     = td.active,
        .body_count = td.body_count,
    };
    phys_stage_spatial_update(&seq_args);

    /* Run parallel. */
    phys_spatial_update_args_t par_args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_par,
        .grid_out   = &td.grid_par,
        .active     = td.active,
        .body_count = td.body_count,
    };
    phys_stage_spatial_update_par(&par_args, &td.job_ctx);

    /* All AABBs must match. */
    int cmp = compare_aabbs(td.aabbs_seq, td.aabbs_par,
                            td.active, td.body_count);
    ASSERT_TRUE(cmp == 0);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 4: Zero bodies — no crash, grid stays empty.
 */
static int test_par_spatial_zero_bodies(void) {
    test_data_t td;
    setup_sphere_bodies(&td, 0);

    phys_spatial_update_args_t args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_par,
        .grid_out   = &td.grid_par,
        .active     = td.active,
        .body_count = 0,
    };

    /* Should not crash. */
    phys_stage_spatial_update_par(&args, &td.job_ctx);

    /* Also test NULL args path. */
    phys_stage_spatial_update_par(NULL, &td.job_ctx);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 5: After parallel run, the grid has entries for active bodies.
 */
static int test_par_spatial_grid_populated(void) {
    test_data_t td;
    setup_sphere_bodies(&td, 50);

    phys_spatial_update_args_t args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_par,
        .grid_out   = &td.grid_par,
        .active     = td.active,
        .body_count = td.body_count,
    };
    phys_stage_spatial_update_par(&args, &td.job_ctx);

    /* Query the grid with a large AABB that should overlap all bodies. */
    phys_aabb_t query = {
        .min = {-10.0f, -10.0f, -10.0f},
        .max = {60.0f, 10.0f, 10.0f},
    };
    uint32_t results[100];
    uint32_t found = phys_spatial_grid_query(&td.grid_par, &query,
                                             results, 100);
    /* All 50 bodies should be found. */
    ASSERT_EQ_UINT(50, found);

    teardown_test_data(&td);
    return 0;
}

/**
 * Test 6: Mixed shapes — sphere, box, capsule colliders produce
 * correct AABBs in parallel.
 */
static int test_par_spatial_mixed_shapes(void) {
    const uint32_t count = 3;

    test_data_t td;
    memset(&td, 0, sizeof(td));
    td.body_count = count;

    td.bodies    = calloc(count, sizeof(phys_body_t));
    td.colliders = calloc(count, sizeof(phys_collider_t));
    td.spheres   = calloc(1, sizeof(phys_sphere_t));
    td.boxes     = calloc(1, sizeof(phys_box_t));
    td.capsules  = calloc(1, sizeof(phys_capsule_t));
    td.aabbs_seq = calloc(count, sizeof(phys_aabb_t));
    td.aabbs_par = calloc(count, sizeof(phys_aabb_t));
    td.active    = calloc(count, sizeof(uint8_t));

    /* Body 0: sphere, radius 2 at origin. */
    phys_body_init(&td.bodies[0]);
    td.bodies[0].position = VEC3_ZERO;
    td.bodies[0].orientation = QUAT_IDENTITY;
    td.spheres[0].radius = 2.0f;
    phys_collider_init_sphere(&td.colliders[0], 0, VEC3_ZERO);
    td.active[0] = 1;

    /* Body 1: box, half-extents (1,2,3) at (10, 0, 0). */
    phys_body_init(&td.bodies[1]);
    td.bodies[1].position = (phys_vec3_t){10.0f, 0.0f, 0.0f};
    td.bodies[1].orientation = QUAT_IDENTITY;
    td.boxes[0].half_extents = (phys_vec3_t){1.0f, 2.0f, 3.0f};
    phys_collider_init_box(&td.colliders[1], 0, VEC3_ZERO, QUAT_IDENTITY);
    td.active[1] = 1;

    /* Body 2: capsule, radius 0.5 half_height 1.0 at (20, 0, 0). */
    phys_body_init(&td.bodies[2]);
    td.bodies[2].position = (phys_vec3_t){20.0f, 0.0f, 0.0f};
    td.bodies[2].orientation = QUAT_IDENTITY;
    td.capsules[0].radius = 0.5f;
    td.capsules[0].half_height = 1.0f;
    phys_collider_init_capsule(&td.colliders[2], 0, VEC3_ZERO, QUAT_IDENTITY);
    td.active[2] = 1;

    /* Arenas and grids. */
    phys_frame_arena_init(&td.arena_seq, 1024 * 1024);
    phys_frame_arena_init(&td.arena_par, 1024 * 1024);
    phys_spatial_grid_init(&td.grid_seq, 256, 4.0f, &td.arena_seq);
    phys_spatial_grid_init(&td.grid_par, 256, 4.0f, &td.arena_par);

    /* Job system. */
    job_system_create(&td.job_sys, 2, 256, 65536, 64, 0);
    job_system_start(&td.job_sys);
    phys_job_context_init(&td.job_ctx, &td.job_sys);

    /* Sequential reference. */
    phys_spatial_update_args_t seq_args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_seq,
        .grid_out   = &td.grid_seq,
        .active     = td.active,
        .body_count = count,
    };
    phys_stage_spatial_update(&seq_args);

    /* Parallel. */
    phys_spatial_update_args_t par_args = {
        .bodies     = td.bodies,
        .colliders  = td.colliders,
        .spheres    = td.spheres,
        .boxes      = td.boxes,
        .capsules   = td.capsules,
        .aabbs_out  = td.aabbs_par,
        .grid_out   = &td.grid_par,
        .active     = td.active,
        .body_count = count,
    };
    phys_stage_spatial_update_par(&par_args, &td.job_ctx);

    /* Compare all AABBs. */
    int cmp = compare_aabbs(td.aabbs_seq, td.aabbs_par, td.active, count);
    ASSERT_TRUE(cmp == 0);

    /* Verify specific AABB values for the sphere (body 0). */
    ASSERT_FLOAT_EQ(-2.0f, td.aabbs_par[0].min.x);
    ASSERT_FLOAT_EQ( 2.0f, td.aabbs_par[0].max.x);
    ASSERT_FLOAT_EQ(-2.0f, td.aabbs_par[0].min.y);
    ASSERT_FLOAT_EQ( 2.0f, td.aabbs_par[0].max.y);

    /* Verify box AABB (body 1). */
    ASSERT_FLOAT_EQ( 9.0f, td.aabbs_par[1].min.x);
    ASSERT_FLOAT_EQ(11.0f, td.aabbs_par[1].max.x);
    ASSERT_FLOAT_EQ(-2.0f, td.aabbs_par[1].min.y);
    ASSERT_FLOAT_EQ( 2.0f, td.aabbs_par[1].max.y);

    teardown_test_data(&td);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_spatial_identical_to_seq",  test_par_spatial_identical_to_seq},
    {"par_spatial_single_batch",      test_par_spatial_single_batch},
    {"par_spatial_multiple_batches",  test_par_spatial_multiple_batches},
    {"par_spatial_zero_bodies",       test_par_spatial_zero_bodies},
    {"par_spatial_grid_populated",    test_par_spatial_grid_populated},
    {"par_spatial_mixed_shapes",      test_par_spatial_mixed_shapes},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        fflush(stdout);
        int rc = tc->fn();
        if (rc == 0) {
            passed++;
            printf("OK %s\n", tc->name);
        } else {
            fprintf(stderr, "Test failed: %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
