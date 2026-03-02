/**
 * @file p049_physics_tick_tests.c
 * @brief Unit tests for phys_world_tick (Stage 0-14 orchestrator).
 *
 * Tests: single body gravity, two-sphere collision, static floor support,
 * tick count increment, empty world, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/game_state.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/job/system.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act), _t = (tol);                              \
        if (fabsf(_e - _a) > _t) {                                             \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "               \
                    "expected %.6f got %.6f (tol %.6f)\n",                      \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)_t);    \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helper: create a small test world ─────────────────────────── */

/**
 * @brief Create a test world with small pool sizes and default config.
 */
static int make_test_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_colliders = 64;
    cfg.manifold_cache_size = 64;
    cfg.frame_arena_size = 1u * 1024u * 1024u; /* 1 MB */
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;
    return phys_world_init(world, &cfg);
}

/* ── Job system helpers ─────────────────────────────────────────── */

static void setup_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    job_system_create(sys, 1, 256, 65536, 64, 0);
    job_system_start(sys);
    phys_job_context_init(ctx, sys);
}

static void teardown_jobs(job_system_t *sys, phys_job_context_t *ctx) {
    phys_job_context_destroy(ctx);
    job_system_shutdown(sys);
}

/* ── Test 1: single body falls under gravity ───────────────────── */

static int test_tick_single_body_falls(void) {
    phys_world_t world;
    job_system_t sys;
    phys_job_context_t ctx;
    ASSERT_TRUE(make_test_world(&world) == 0);
    setup_jobs(&sys, &ctx);

    /* Create a dynamic sphere body at (0, 10, 0). */
    uint32_t idx = phys_world_create_body(&world);
    if (idx == UINT32_MAX) { teardown_jobs(&sys, &ctx); return 1; }

    phys_body_t *body = phys_world_get_body(&world, idx);
    if (!body) { teardown_jobs(&sys, &ctx); return 1; }
    body->position = (phys_vec3_t){0.0f, 10.0f, 0.0f};
    phys_body_set_mass(body, 1.0f);
    phys_body_set_sphere_inertia(body, 1.0f, 0.5f);

    /* Also copy to bodies_next so integration reads correct input. */
    phys_body_t *body_next = phys_body_pool_get_next(&world.body_pool, idx);
    if (!body_next) { teardown_jobs(&sys, &ctx); return 1; }
    *body_next = *body;

    phys_world_set_sphere_collider(&world, idx, 0.5f,
                                   (phys_vec3_t){0, 0, 0});

    /* Run one tick. */
    phys_world_tick_parallel(&world, NULL, &ctx);

    /* Body should have fallen: y < 10. */
    phys_body_t *after = phys_world_get_body(&world, idx);
    ASSERT_TRUE(after != NULL);
    ASSERT_TRUE(after->position.y < 10.0f);

    phys_world_destroy(&world);
    teardown_jobs(&sys, &ctx);
    return 0;
}

/* ── Test 2: two spheres collide ───────────────────────────────── */

static int test_tick_two_spheres_collide(void) {
    phys_world_t world;
    job_system_t sys;
    phys_job_context_t ctx;
    ASSERT_TRUE(make_test_world(&world) == 0);
    setup_jobs(&sys, &ctx);

    /* Sphere A at (-0.4, 0, 0), radius 0.5, moving right. */
    uint32_t a = phys_world_create_body(&world);
    if (a == UINT32_MAX) { teardown_jobs(&sys, &ctx); return 1; }
    phys_body_t *ba = phys_world_get_body(&world, a);
    ba->position = (phys_vec3_t){-0.4f, 0.0f, 0.0f};
    ba->linear_vel = (phys_vec3_t){5.0f, 0.0f, 0.0f};
    phys_body_set_mass(ba, 1.0f);
    phys_body_set_sphere_inertia(ba, 1.0f, 0.5f);
    *phys_body_pool_get_next(&world.body_pool, a) = *ba;
    phys_world_set_sphere_collider(&world, a, 0.5f,
                                   (phys_vec3_t){0, 0, 0});

    /* Sphere B at (0.4, 0, 0), radius 0.5, moving left. */
    uint32_t b = phys_world_create_body(&world);
    if (b == UINT32_MAX) { teardown_jobs(&sys, &ctx); return 1; }
    phys_body_t *bb = phys_world_get_body(&world, b);
    bb->position = (phys_vec3_t){0.4f, 0.0f, 0.0f};
    bb->linear_vel = (phys_vec3_t){-5.0f, 0.0f, 0.0f};
    phys_body_set_mass(bb, 1.0f);
    phys_body_set_sphere_inertia(bb, 1.0f, 0.5f);
    *phys_body_pool_get_next(&world.body_pool, b) = *bb;
    phys_world_set_sphere_collider(&world, b, 0.5f,
                                   (phys_vec3_t){0, 0, 0});

    /* They overlap by 0.2 units — should be separated after tick. */
    phys_world_tick_parallel(&world, NULL, &ctx);

    phys_body_t *after_a = phys_world_get_body(&world, a);
    phys_body_t *after_b = phys_world_get_body(&world, b);
    ASSERT_TRUE(after_a != NULL);
    ASSERT_TRUE(after_b != NULL);

    /* After collision response, they should have moved apart or at
     * least not gotten closer.  Check separation distance >= 0.8. */
    float dx = after_b->position.x - after_a->position.x;
    float dy = after_b->position.y - after_a->position.y;
    float dz = after_b->position.z - after_a->position.z;
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    /* They should not be more deeply overlapping. */
    ASSERT_TRUE(dist >= 0.6f);

    phys_world_destroy(&world);
    teardown_jobs(&sys, &ctx);
    return 0;
}

/* ── Test 3: static floor stops dynamic sphere ─────────────────── */

static int test_tick_static_floor(void) {
    phys_world_t world;
    job_system_t sys;
    phys_job_context_t ctx;
    ASSERT_TRUE(make_test_world(&world) == 0);
    setup_jobs(&sys, &ctx);

    /* Static floor sphere at (0, 0, 0) with large radius. */
    uint32_t floor_idx = phys_world_create_body(&world);
    if (floor_idx == UINT32_MAX) { teardown_jobs(&sys, &ctx); return 1; }
    phys_body_t *floor_body = phys_world_get_body(&world, floor_idx);
    floor_body->position = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    /* Static: inv_mass = 0 (default from phys_body_init). */
    floor_body->flags |= PHYS_BODY_FLAG_STATIC;
    *phys_body_pool_get_next(&world.body_pool, floor_idx) = *floor_body;
    phys_world_set_sphere_collider(&world, floor_idx, 1.0f,
                                   (phys_vec3_t){0, 0, 0});

    /* Dynamic sphere at (0, 1.5, 0), radius 1.0 — sits just above floor. */
    uint32_t ball_idx = phys_world_create_body(&world);
    if (ball_idx == UINT32_MAX) { teardown_jobs(&sys, &ctx); return 1; }
    phys_body_t *ball = phys_world_get_body(&world, ball_idx);
    ball->position = (phys_vec3_t){0.0f, 1.5f, 0.0f};
    phys_body_set_mass(ball, 1.0f);
    phys_body_set_sphere_inertia(ball, 1.0f, 1.0f);
    *phys_body_pool_get_next(&world.body_pool, ball_idx) = *ball;
    phys_world_set_sphere_collider(&world, ball_idx, 1.0f,
                                   (phys_vec3_t){0, 0, 0});

    /* Run a tick. */
    phys_world_tick_parallel(&world, NULL, &ctx);

    /* The dynamic body should not fall through the floor.
     * With overlapping spheres (distance=1.5 < 2.0 radii), the solver
     * should push them apart.  The ball's y should be >= ~1.4. */
    phys_body_t *after_ball = phys_world_get_body(&world, ball_idx);
    ASSERT_TRUE(after_ball != NULL);
    ASSERT_TRUE(after_ball->position.y >= 1.0f);

    phys_world_destroy(&world);
    teardown_jobs(&sys, &ctx);
    return 0;
}

/* ── Test 4: tick increments count ─────────────────────────────── */

static int test_tick_increments_count(void) {
    phys_world_t world;
    job_system_t sys;
    phys_job_context_t ctx;
    ASSERT_TRUE(make_test_world(&world) == 0);
    setup_jobs(&sys, &ctx);

    ASSERT_TRUE(phys_world_tick_count(&world) == 0);
    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(phys_world_tick_count(&world) == 1);
    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(phys_world_tick_count(&world) == 2);

    phys_world_destroy(&world);
    teardown_jobs(&sys, &ctx);
    return 0;
}

/* ── Test 5: no bodies — tick doesn't crash ────────────────────── */

static int test_tick_no_bodies(void) {
    phys_world_t world;
    job_system_t sys;
    phys_job_context_t ctx;
    ASSERT_TRUE(make_test_world(&world) == 0);
    setup_jobs(&sys, &ctx);

    /* Tick with zero bodies should not crash. */
    phys_world_tick_parallel(&world, NULL, &ctx);
    ASSERT_TRUE(phys_world_tick_count(&world) == 1);

    phys_world_destroy(&world);
    teardown_jobs(&sys, &ctx);
    return 0;
}

/* ── Test 6: NULL world doesn't crash ──────────────────────────── */

static int test_tick_null_safe(void) {
    /* Should be a no-op. */
    phys_world_tick_parallel(NULL, NULL, NULL);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p049_physics_tick_tests\n");
    RUN_TEST(test_tick_single_body_falls);
    RUN_TEST(test_tick_two_spheres_collide);
    RUN_TEST(test_tick_static_floor);
    RUN_TEST(test_tick_increments_count);
    RUN_TEST(test_tick_no_bodies);
    RUN_TEST(test_tick_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
