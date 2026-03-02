/**
 * @file p071_physics_par_tick_tests.c
 * @brief Tests for the parallel tick entry point (phys-312).
 *
 * Each test runs the same scenario through both phys_world_tick() and
 * phys_world_tick_parallel(), then compares body positions and velocities
 * to verify identical results.
 */

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/job/system.h"

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

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            TEST_FAIL("expected %.6f got %.6f (tol %.6f)",                     \
                      (double)_e, (double)_a, (double)_t);                     \
        }                                                                       \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ── Identity quaternion constant ──────────────────────────────── */

static const phys_quat_t QUAT_IDENTITY = {.x = 0, .y = 0, .z = 0, .w = 1};

/* Float tolerance for comparing positions/velocities. */
#define TOLERANCE 1e-5f

/* ── World setup helpers ───────────────────────────────────────── */

/**
 * @brief Create a test world with reasonable defaults.
 * @return 0 on success, -1 on failure.
 */
static int make_test_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 128;
    cfg.max_colliders = 128;
    cfg.manifold_cache_size = 128;
    cfg.frame_arena_size = 4u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;
    return phys_world_init(world, &cfg);
}

/**
 * @brief Create a dynamic sphere in the world.
 */
static uint32_t create_dynamic_sphere(phys_world_t *world,
                                      phys_vec3_t pos,
                                      phys_vec3_t vel,
                                      float mass, float radius) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = vel;
    phys_body_set_mass(body, mass);
    phys_body_set_sphere_inertia(body, mass, radius);

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_sphere_collider(world, idx, radius,
                                   (phys_vec3_t){0, 0, 0});
    return idx;
}

/**
 * @brief Create a dynamic box in the world.
 */
static uint32_t create_dynamic_box(phys_world_t *world,
                                   phys_vec3_t pos,
                                   phys_vec3_t vel,
                                   float mass,
                                   phys_vec3_t half_extents) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = vel;
    phys_body_set_mass(body, mass);
    phys_body_set_box_inertia(body, mass, half_extents);

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_box_collider(world, idx, half_extents,
                                (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
    return idx;
}

/**
 * @brief Create a dynamic capsule in the world.
 */
static uint32_t create_dynamic_capsule(phys_world_t *world,
                                       phys_vec3_t pos,
                                       phys_vec3_t vel,
                                       float mass,
                                       float radius,
                                       float half_height) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = vel;
    phys_body_set_mass(body, mass);
    phys_body_set_capsule_inertia(body, mass, radius, half_height);

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_capsule_collider(world, idx, radius, half_height,
                                    (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
    return idx;
}

/* ── Job system helpers ────────────────────────────────────────── */

/**
 * @brief Create and start a deterministic job system + physics job context.
 */
static void setup_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    job_system_create(sys, 1, 256, 65536, 64, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}

/**
 * @brief Tear down job context and job system.
 */
static void teardown_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

/* ── Comparison helpers ────────────────────────────────────────── */

/**
 * @brief Compare all body positions and velocities between two worlds.
 * @return 0 if all match within TOLERANCE, 1 on mismatch.
 */
static int compare_worlds(const phys_world_t *seq,
                          const phys_world_t *par,
                          uint32_t body_cap) {
    for (uint32_t i = 0; i < body_cap; i++) {
        if (!seq->body_pool.active[i]) continue;

        const phys_body_t *bs = &seq->body_pool.bodies_curr[i];
        const phys_body_t *bp = &par->body_pool.bodies_curr[i];

        /* Position. */
        if (fabsf(bs->position.x - bp->position.x) > TOLERANCE ||
            fabsf(bs->position.y - bp->position.y) > TOLERANCE ||
            fabsf(bs->position.z - bp->position.z) > TOLERANCE) {
            fprintf(stderr,
                    "  body %u position mismatch: seq(%.6f,%.6f,%.6f) "
                    "par(%.6f,%.6f,%.6f)\n", i,
                    (double)bs->position.x, (double)bs->position.y,
                    (double)bs->position.z,
                    (double)bp->position.x, (double)bp->position.y,
                    (double)bp->position.z);
            return 1;
        }

        /* Linear velocity. */
        if (fabsf(bs->linear_vel.x - bp->linear_vel.x) > TOLERANCE ||
            fabsf(bs->linear_vel.y - bp->linear_vel.y) > TOLERANCE ||
            fabsf(bs->linear_vel.z - bp->linear_vel.z) > TOLERANCE) {
            fprintf(stderr,
                    "  body %u linear_vel mismatch: seq(%.6f,%.6f,%.6f) "
                    "par(%.6f,%.6f,%.6f)\n", i,
                    (double)bs->linear_vel.x, (double)bs->linear_vel.y,
                    (double)bs->linear_vel.z,
                    (double)bp->linear_vel.x, (double)bp->linear_vel.y,
                    (double)bp->linear_vel.z);
            return 1;
        }

        /* Angular velocity. */
        if (fabsf(bs->angular_vel.x - bp->angular_vel.x) > TOLERANCE ||
            fabsf(bs->angular_vel.y - bp->angular_vel.y) > TOLERANCE ||
            fabsf(bs->angular_vel.z - bp->angular_vel.z) > TOLERANCE) {
            fprintf(stderr,
                    "  body %u angular_vel mismatch: seq(%.6f,%.6f,%.6f) "
                    "par(%.6f,%.6f,%.6f)\n", i,
                    (double)bs->angular_vel.x, (double)bs->angular_vel.y,
                    (double)bs->angular_vel.z,
                    (double)bp->angular_vel.x, (double)bp->angular_vel.y,
                    (double)bp->angular_vel.z);
            return 1;
        }
    }
    return 0;
}

/**
 * @brief Clone a world by creating an identical copy.
 *
 * Creates a second world with the same config and creates matching
 * bodies with the same state.  Copies body data, colliders, shapes,
 * and active flags.
 *
 * @return 0 on success, -1 on failure.
 */
static int clone_world(const phys_world_t *src, phys_world_t *dst) {
    if (phys_world_init(dst, &src->config) != 0) {
        return -1;
    }
    uint32_t cap = src->body_pool.capacity;

    /* Copy body pool state. */
    memcpy(dst->body_pool.bodies_curr, src->body_pool.bodies_curr,
           cap * sizeof(phys_body_t));
    memcpy(dst->body_pool.bodies_next, src->body_pool.bodies_next,
           cap * sizeof(phys_body_t));
    memcpy(dst->body_pool.active, src->body_pool.active,
           cap * sizeof(uint8_t));
    dst->body_pool.count = src->body_pool.count;

    /* Copy colliders and shapes. */
    memcpy(dst->colliders, src->colliders, cap * sizeof(phys_collider_t));
    memcpy(dst->aabbs, src->aabbs, cap * sizeof(phys_aabb_t));

    /* Copy shape arrays. */
    uint32_t sc = src->sphere_count;
    uint32_t bc = src->box_count;
    uint32_t cc = src->capsule_count;
    if (sc > 0) memcpy(dst->spheres, src->spheres, sc * sizeof(phys_sphere_t));
    if (bc > 0) memcpy(dst->boxes, src->boxes, bc * sizeof(phys_box_t));
    if (cc > 0) memcpy(dst->capsules, src->capsules, cc * sizeof(phys_capsule_t));
    dst->sphere_count = sc;
    dst->box_count = bc;
    dst->capsule_count = cc;

    dst->tick_count = src->tick_count;

    return 0;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: 20 dynamic spheres falling under gravity.
 * Sequential and parallel results must match.
 */
static int test_par_tick_matches_seq(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Create 20 dynamic spheres spread out to avoid collisions. */
    for (int i = 0; i < 20; i++) {
        float x = (float)(i % 5) * 3.0f;
        float z = (float)(i / 5) * 3.0f;
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, 5.0f, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* Clone world for parallel run. */
    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    /* Set up job system. */
    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run sequential. */
    phys_world_tick_parallel(&world, NULL, &ctx);

    /* Run parallel. */
    phys_world_tick_parallel(&world_par, NULL, &ctx);

    /* Compare results. */
    int cmp = compare_worlds(&world, &world_par, world.body_pool.capacity);
    ASSERT_TRUE(cmp == 0);

    /* Verify tick counts match. */
    ASSERT_TRUE(world.tick_count == world_par.tick_count);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 2: 10 bodies placed close together so collisions occur.
 * Sequential and parallel results must match.
 */
static int test_par_tick_with_collisions(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Place 10 spheres in a tight cluster — collisions expected. */
    for (int i = 0; i < 10; i++) {
        float x = (float)(i % 3) * 0.8f;
        float y = 2.0f + (float)(i / 3) * 0.8f;
        float z = 0.0f;
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, -1.0f, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    phys_world_tick_parallel(&world, NULL, &ctx);
    phys_world_tick_parallel(&world_par, NULL, &ctx);

    int cmp = compare_worlds(&world, &world_par, world.body_pool.capacity);
    ASSERT_TRUE(cmp == 0);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 3: Empty world — 0 bodies. No crash.
 */
static int test_par_tick_empty_world(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Both ticks should be no-ops without crashing. */
    phys_world_tick_parallel(&world, NULL, &ctx);
    phys_world_tick_parallel(&world_par, NULL, &ctx);

    ASSERT_TRUE(world.tick_count == 1);
    ASSERT_TRUE(world_par.tick_count == 1);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 4: Single body under gravity. Exact match.
 */
static int test_par_tick_single_body(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    uint32_t idx = create_dynamic_sphere(
        &world,
        (phys_vec3_t){0, 10.0f, 0},
        (phys_vec3_t){0, 0, 0},
        1.0f, 0.5f);
    ASSERT_TRUE(idx != UINT32_MAX);

    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    phys_world_tick_parallel(&world, NULL, &ctx);
    phys_world_tick_parallel(&world_par, NULL, &ctx);

    int cmp = compare_worlds(&world, &world_par, world.body_pool.capacity);
    ASSERT_TRUE(cmp == 0);

    /* Verify gravity took effect. */
    const phys_body_t *body = &world.body_pool.bodies_curr[idx];
    ASSERT_TRUE(body->position.y < 10.0f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 5: Mixed shapes — spheres, boxes, capsules. Results match.
 */
static int test_par_tick_mixed_shapes(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Spheres. */
    for (int i = 0; i < 4; i++) {
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){(float)i * 3.0f, 5.0f, 0},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* Boxes. */
    for (int i = 0; i < 4; i++) {
        uint32_t idx = create_dynamic_box(
            &world,
            (phys_vec3_t){(float)i * 3.0f, 5.0f, 3.0f},
            (phys_vec3_t){0, 0, 0},
            1.0f,
            (phys_vec3_t){0.5f, 0.5f, 0.5f});
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* Capsules. */
    for (int i = 0; i < 4; i++) {
        uint32_t idx = create_dynamic_capsule(
            &world,
            (phys_vec3_t){(float)i * 3.0f, 5.0f, 6.0f},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.3f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    phys_world_tick_parallel(&world, NULL, &ctx);
    phys_world_tick_parallel(&world_par, NULL, &ctx);

    int cmp = compare_worlds(&world, &world_par, world.body_pool.capacity);
    ASSERT_TRUE(cmp == 0);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Test 6: Run 10 ticks. Results match after all ticks.
 */
static int test_par_tick_multi_substep(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Create a few bodies. */
    for (int i = 0; i < 5; i++) {
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){(float)i * 2.0f, 8.0f, 0},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run 10 ticks on both worlds. */
    for (int t = 0; t < 10; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
        phys_world_tick_parallel(&world_par, NULL, &ctx);

        /* Compare after each tick to catch divergence early. */
        int cmp = compare_worlds(&world, &world_par,
                                 world.body_pool.capacity);
        if (cmp != 0) {
            fprintf(stderr, "  mismatch at tick %d\n", t + 1);
            ASSERT_TRUE(cmp == 0);
        }
    }

    ASSERT_TRUE(world.tick_count == 10);
    ASSERT_TRUE(world_par.tick_count == 10);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"par_tick_matches_seq",     test_par_tick_matches_seq},
    {"par_tick_with_collisions", test_par_tick_with_collisions},
    {"par_tick_empty_world",     test_par_tick_empty_world},
    {"par_tick_single_body",     test_par_tick_single_body},
    {"par_tick_mixed_shapes",    test_par_tick_mixed_shapes},
    {"par_tick_multi_substep",   test_par_tick_multi_substep},
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
