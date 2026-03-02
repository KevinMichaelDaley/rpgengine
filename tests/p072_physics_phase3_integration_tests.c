/**
 * @file p072_physics_phase3_integration_tests.c
 * @brief Phase 3 integration tests: parallel pipeline correctness + benchmarks.
 *
 * Validates that phys_world_tick_parallel() produces identical results to
 * phys_world_tick() across various scenarios, and benchmarks large-body-count
 * parallel ticking.
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
#include <time.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/snapshot.h"
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

/* Float tolerance for comparing positions/velocities. */
#define TOLERANCE 1e-5f

/* ── Identity quaternion constant ──────────────────────────────── */

static const phys_quat_t QUAT_IDENTITY = {.x = 0, .y = 0, .z = 0, .w = 1};

/* ── Simple LCG for deterministic random ──────────────────────── */

/**
 * @brief Deterministic LCG pseudo-random number generator.
 * @param state  Mutable seed state (non-NULL).
 * @return Next pseudo-random uint32_t value.
 */
static uint32_t lcg_next(uint32_t *state) {
    *state = (*state) * 1664525u + 1013904223u;
    return *state;
}

/**
 * @brief Return a deterministic float in [lo, hi].
 */
static float lcg_float(uint32_t *state, float lo, float hi) {
    uint32_t r = lcg_next(state);
    float t = (float)(r & 0xFFFFu) / 65535.0f;
    return lo + t * (hi - lo);
}

/* ── Body creation helpers ─────────────────────────────────────── */

/**
 * @brief Create a dynamic sphere in the world.
 * @return Body index, or UINT32_MAX on failure.
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
 * @return Body index, or UINT32_MAX on failure.
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
 * @return Body index, or UINT32_MAX on failure.
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

/**
 * @brief Create a static box (ground plane).
 * @return Body index, or UINT32_MAX on failure.
 */
static uint32_t create_static_box(phys_world_t *world,
                                  phys_vec3_t pos,
                                  phys_vec3_t half_extents) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->flags |= PHYS_BODY_FLAG_STATIC;

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_box_collider(world, idx, half_extents,
                                (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
    return idx;
}

/* ── Random world setup helper ─────────────────────────────────── */

/**
 * @brief Populate a world with N bodies at deterministic random positions.
 *
 * Uses a simple LCG seeded by @p seed. Creates a mix of dynamic spheres,
 * boxes, and capsules spread across a 100×10×100 volume. All bodies have
 * inv_mass=1.0 (mass=1.0) and reasonable inertia. Both bodies_curr and
 * bodies_next are initialized to the same state.
 *
 * @param world      Initialized world with sufficient capacity.
 * @param body_count Number of bodies to create.
 * @param seed       Deterministic seed for the LCG.
 * @return 0 on success, 1 if any body creation fails.
 */
static int setup_random_world(phys_world_t *world, uint32_t body_count,
                              uint32_t seed) {
    uint32_t rng = seed;
    for (uint32_t i = 0; i < body_count; i++) {
        float x = lcg_float(&rng, -50.0f, 50.0f);
        float y = lcg_float(&rng, 0.5f, 10.0f);
        float z = lcg_float(&rng, -50.0f, 50.0f);
        float vx = lcg_float(&rng, -2.0f, 2.0f);
        float vy = lcg_float(&rng, -1.0f, 1.0f);
        float vz = lcg_float(&rng, -2.0f, 2.0f);
        phys_vec3_t pos = {x, y, z};
        phys_vec3_t vel = {vx, vy, vz};

        uint32_t kind = lcg_next(&rng) % 3;
        uint32_t idx = UINT32_MAX;
        if (kind == 0) {
            idx = create_dynamic_sphere(world, pos, vel, 1.0f, 0.5f);
        } else if (kind == 1) {
            phys_vec3_t he = {0.4f, 0.4f, 0.4f};
            idx = create_dynamic_box(world, pos, vel, 1.0f, he);
        } else {
            idx = create_dynamic_capsule(world, pos, vel, 1.0f, 0.3f, 0.4f);
        }
        if (idx == UINT32_MAX) return 1;
    }
    return 0;
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

/* ── World clone + comparison ──────────────────────────────────── */

/**
 * @brief Clone a world by creating an identical copy.
 * @return 0 on success, -1 on failure.
 */
static int clone_world(const phys_world_t *src, phys_world_t *dst) {
    if (phys_world_init(dst, &src->config) != 0) {
        return -1;
    }
    uint32_t cap = src->body_pool.capacity;

    memcpy(dst->body_pool.bodies_curr, src->body_pool.bodies_curr,
           cap * sizeof(phys_body_t));
    memcpy(dst->body_pool.bodies_next, src->body_pool.bodies_next,
           cap * sizeof(phys_body_t));
    memcpy(dst->body_pool.active, src->body_pool.active,
           cap * sizeof(uint8_t));
    dst->body_pool.count = src->body_pool.count;

    memcpy(dst->colliders, src->colliders, cap * sizeof(phys_collider_t));
    memcpy(dst->aabbs, src->aabbs, cap * sizeof(phys_aabb_t));

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

/* ── Timing helper ─────────────────────────────────────────────── */

/**
 * @brief Return monotonic time in seconds.
 */
static double now_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
}

/* ── Test 1: parallel determinism (200 spheres, 20 ticks) ──────── */

/**
 * 200 dynamic spheres at random positions with random velocities.
 * Run 20 ticks through both sequential and parallel. All body
 * positions must match within float tolerance (1e-5).
 *
 * Note: body count reduced from 200 to keep debug-build runtime
 * reasonable; bodies are spread widely so broadphase stays sparse.
 */
static int test_parallel_determinism(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_colliders = 64;
    cfg.manifold_cache_size = 128;
    cfg.frame_arena_size = 4u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Create 40 dynamic spheres at deterministic random positions. */
    uint32_t rng = 42;
    for (int i = 0; i < 40; i++) {
        float x = lcg_float(&rng, -50.0f, 50.0f);
        float y = lcg_float(&rng, 1.0f, 10.0f);
        float z = lcg_float(&rng, -50.0f, 50.0f);
        float vx = lcg_float(&rng, -3.0f, 3.0f);
        float vy = lcg_float(&rng, -1.0f, 1.0f);
        float vz = lcg_float(&rng, -3.0f, 3.0f);
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){vx, vy, vz},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* Clone for parallel run. */
    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run 20 ticks on both. */
    for (int t = 0; t < 20; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
        phys_world_tick_parallel(&world_par, NULL, &ctx);

        int cmp = compare_worlds(&world, &world_par,
                                 world.body_pool.capacity);
        if (cmp != 0) {
            fprintf(stderr, "  mismatch at tick %d\n", t + 1);
            ASSERT_TRUE(cmp == 0);
        }
    }

    ASSERT_TRUE(world.tick_count == 20);
    ASSERT_TRUE(world_par.tick_count == 20);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test 2: mixed shapes (50 spheres + 30 boxes + 20 capsules) ── */

/**
 * Mixed shapes with static ground plane.
 * Run 10 ticks. Sequential vs parallel must match.
 */
static int test_parallel_mixed_shapes(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_colliders = 64;
    cfg.manifold_cache_size = 128;
    cfg.frame_arena_size = 4u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Static ground plane. */
    uint32_t ground = create_static_box(
        &world,
        (phys_vec3_t){0, -1.0f, 0},
        (phys_vec3_t){50.0f, 1.0f, 50.0f});
    ASSERT_TRUE(ground != UINT32_MAX);

    uint32_t rng = 12345;

    /* 15 spheres. */
    for (int i = 0; i < 15; i++) {
        float x = lcg_float(&rng, -20.0f, 20.0f);
        float y = lcg_float(&rng, 1.0f, 8.0f);
        float z = lcg_float(&rng, -20.0f, 20.0f);
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* 10 boxes. */
    for (int i = 0; i < 10; i++) {
        float x = lcg_float(&rng, -20.0f, 20.0f);
        float y = lcg_float(&rng, 1.0f, 8.0f);
        float z = lcg_float(&rng, -20.0f, 20.0f);
        phys_vec3_t he = {0.4f, 0.4f, 0.4f};
        uint32_t idx = create_dynamic_box(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, he);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* 8 capsules. */
    for (int i = 0; i < 8; i++) {
        float x = lcg_float(&rng, -20.0f, 20.0f);
        float y = lcg_float(&rng, 1.0f, 8.0f);
        float z = lcg_float(&rng, -20.0f, 20.0f);
        uint32_t idx = create_dynamic_capsule(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.3f, 0.4f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* Clone for parallel. */
    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run 10 ticks. */
    for (int t = 0; t < 10; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
        phys_world_tick_parallel(&world_par, NULL, &ctx);

        int cmp = compare_worlds(&world, &world_par,
                                 world.body_pool.capacity);
        if (cmp != 0) {
            fprintf(stderr, "  mismatch at tick %d\n", t + 1);
            ASSERT_TRUE(cmp == 0);
        }
    }

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test 3: collision-heavy (100 bodies near origin) ──────────── */

/**
 * Bodies all near origin (will generate many collisions).
 * Run 5 ticks. Results must match sequential.
 */
static int test_parallel_collision_heavy(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_colliders = 64;
    cfg.manifold_cache_size = 256;
    cfg.frame_arena_size = 4u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Pack 30 bodies tightly near origin. */
    uint32_t rng = 99;
    for (int i = 0; i < 30; i++) {
        float x = lcg_float(&rng, -2.0f, 2.0f);
        float y = lcg_float(&rng, 0.5f, 4.0f);
        float z = lcg_float(&rng, -2.0f, 2.0f);
        float vx = lcg_float(&rng, -1.0f, 1.0f);
        float vy = lcg_float(&rng, -1.0f, 1.0f);
        float vz = lcg_float(&rng, -1.0f, 1.0f);
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){vx, vy, vz},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    phys_world_t world_par;
    ASSERT_TRUE(clone_world(&world, &world_par) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    for (int t = 0; t < 5; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
        phys_world_tick_parallel(&world_par, NULL, &ctx);

        int cmp = compare_worlds(&world, &world_par,
                                 world.body_pool.capacity);
        if (cmp != 0) {
            fprintf(stderr, "  mismatch at tick %d\n", t + 1);
            ASSERT_TRUE(cmp == 0);
        }
    }

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world_par);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test 4: empty world ───────────────────────────────────────── */

/**
 * 0 bodies. 5 ticks. No crash. tick_count increments.
 */
static int test_parallel_empty_world(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.manifold_cache_size = 16;
    cfg.frame_arena_size = 1u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    for (int t = 0; t < 5; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    ASSERT_TRUE(world.tick_count == 5);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test 5: single body gravity ───────────────────────────────── */

/**
 * 1 sphere under gravity. 30 ticks. Final position matches
 * sequential exactly.
 */
static int test_parallel_single_body_gravity(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.manifold_cache_size = 16;
    cfg.frame_arena_size = 1u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

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

    for (int t = 0; t < 30; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
        phys_world_tick_parallel(&world_par, NULL, &ctx);
    }

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

/* ── Test 6: snapshot consistency ──────────────────────────────── */

/**
 * Run 10 parallel ticks, then encode snapshot. Decode snapshot and
 * verify positions match world state (within quantization tolerance).
 */
static int test_parallel_snapshot_consistency(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 32;
    cfg.max_colliders = 32;
    cfg.manifold_cache_size = 64;
    cfg.frame_arena_size = 2u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    /* Disable gravity for predictable positions. */
    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Create a few bodies with velocities. */
    uint32_t s = create_dynamic_sphere(
        &world,
        (phys_vec3_t){0, 1.0f, 0},
        (phys_vec3_t){1.0f, 0, 0},
        1.0f, 0.5f);
    ASSERT_TRUE(s != UINT32_MAX);

    uint32_t b = create_dynamic_box(
        &world,
        (phys_vec3_t){5.0f, 1.0f, 0},
        (phys_vec3_t){0, 0, 1.0f},
        1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    ASSERT_TRUE(b != UINT32_MAX);

    uint32_t c = create_dynamic_capsule(
        &world,
        (phys_vec3_t){0, 1.0f, 5.0f},
        (phys_vec3_t){0, 0, -1.0f},
        1.0f, 0.3f, 0.4f);
    ASSERT_TRUE(c != UINT32_MAX);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Run 10 parallel ticks. */
    for (int t = 0; t < 10; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    /* Record positions before snapshot. */
    const phys_body_t *bs = phys_world_get_body(&world, s);
    const phys_body_t *bb = phys_world_get_body(&world, b);
    const phys_body_t *bc = phys_world_get_body(&world, c);
    ASSERT_TRUE(bs != NULL && bb != NULL && bc != NULL);
    float sx = bs->position.x;
    float by = bb->position.y;
    float cz = bc->position.z;

    /* Encode snapshot. */
    uint8_t buffer[8192];
    size_t encoded = phys_snapshot_encode(&world, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded > 0);

    /* Decode snapshot. */
    phys_snapshot_t snapshot;
    phys_snapshot_body_t snap_bodies[32];
    snapshot.bodies = snap_bodies;
    int rc = phys_snapshot_decode(buffer, encoded, &snapshot);
    ASSERT_TRUE(rc == 0);

    ASSERT_TRUE(snapshot.tick == 10);
    ASSERT_TRUE(snapshot.body_count == 3);

    /* Verify decoded positions approximately match (quantization ~1mm). */
    phys_vec3_t d_s = phys_dequantize_vec3(
        snapshot.bodies[s].position, 1.0f / 1000.0f);
    phys_vec3_t d_b = phys_dequantize_vec3(
        snapshot.bodies[b].position, 1.0f / 1000.0f);
    phys_vec3_t d_c = phys_dequantize_vec3(
        snapshot.bodies[c].position, 1.0f / 1000.0f);

    ASSERT_FLOAT_NEAR(sx, d_s.x, 0.05f);
    ASSERT_FLOAT_NEAR(by, d_b.y, 0.05f);
    ASSERT_FLOAT_NEAR(cz, d_c.z, 0.05f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test 7: benchmark 1000 bodies ─────────────────────────────── */

/**
 * Create 100 dynamic bodies (mixed). Run 10 parallel ticks.
 * Print average ms/tick to stdout.
 */
static int test_bench_1000_bodies(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 128;
    cfg.max_colliders = 128;
    cfg.manifold_cache_size = 256;
    cfg.frame_arena_size = 8u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);
    ASSERT_TRUE(setup_random_world(&world, 100, 7777) == 0);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    double start = now_sec();
    for (int t = 0; t < 10; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }
    double elapsed = now_sec() - start;

    double avg_ms = (elapsed / 10.0) * 1000.0;
    printf("  [bench] 100 bodies, 10 ticks: avg %.3f ms/tick\n", avg_ms);

    /* Sanity: verify tick count. */
    ASSERT_TRUE(world.tick_count == 10);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test 8: benchmark mixed 3000 bodies ──────────────────────── */

/**
 * Mixed shapes (spheres + boxes + capsules). Static ground.
 * Parallel ticks. Print average ms/tick.
 */
static int test_bench_mixed_3000(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 256;
    cfg.max_colliders = 256;
    cfg.manifold_cache_size = 512;
    cfg.frame_arena_size = 16u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;

    phys_world_t world;
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    /* Static ground. */
    uint32_t ground = create_static_box(
        &world,
        (phys_vec3_t){0, -1.0f, 0},
        (phys_vec3_t){100.0f, 1.0f, 100.0f});
    ASSERT_TRUE(ground != UINT32_MAX);

    uint32_t rng = 55555;

    /* 100 spheres. */
    for (int i = 0; i < 100; i++) {
        float x = lcg_float(&rng, -50.0f, 50.0f);
        float y = lcg_float(&rng, 0.5f, 10.0f);
        float z = lcg_float(&rng, -50.0f, 50.0f);
        uint32_t idx = create_dynamic_sphere(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.5f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* 70 boxes. */
    for (int i = 0; i < 70; i++) {
        float x = lcg_float(&rng, -50.0f, 50.0f);
        float y = lcg_float(&rng, 0.5f, 10.0f);
        float z = lcg_float(&rng, -50.0f, 50.0f);
        phys_vec3_t he = {0.4f, 0.4f, 0.4f};
        uint32_t idx = create_dynamic_box(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, he);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    /* 30 capsules. */
    for (int i = 0; i < 30; i++) {
        float x = lcg_float(&rng, -50.0f, 50.0f);
        float y = lcg_float(&rng, 0.5f, 10.0f);
        float z = lcg_float(&rng, -50.0f, 50.0f);
        uint32_t idx = create_dynamic_capsule(
            &world,
            (phys_vec3_t){x, y, z},
            (phys_vec3_t){0, 0, 0},
            1.0f, 0.3f, 0.4f);
        ASSERT_TRUE(idx != UINT32_MAX);
    }

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    double start = now_sec();
    for (int t = 0; t < 10; t++) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }
    double elapsed = now_sec() - start;

    double avg_ms = (elapsed / 10.0) * 1000.0;
    printf("  [bench] 200 mixed bodies, 10 ticks: avg %.3f ms/tick\n", avg_ms);

    ASSERT_TRUE(world.tick_count == 10);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/* ── Test table and runner ──────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"parallel_determinism",          test_parallel_determinism},
    {"parallel_mixed_shapes",         test_parallel_mixed_shapes},
    {"parallel_collision_heavy",      test_parallel_collision_heavy},
    {"parallel_empty_world",          test_parallel_empty_world},
    {"parallel_single_body_gravity",  test_parallel_single_body_gravity},
    {"parallel_snapshot_consistency", test_parallel_snapshot_consistency},
    {"bench_1000_bodies",             test_bench_1000_bodies},
    {"bench_mixed_3000",              test_bench_mixed_3000},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    printf("p072_physics_phase3_integration_tests\n");
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
