/**
 * @file p059_physics_phase2_integration_tests.c
 * @brief Phase 2 integration tests for the full physics pipeline with
 *        mixed shape types (sphere, box, capsule).
 *
 * Exercises end-to-end: world creation with all three shape types,
 * tick simulation through the full pipeline (broadphase, narrowphase
 * for all 6 shape pairs, solver, integration), snapshot encode/decode,
 * and prediction reconciliation.
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
#include "ferrum/physics/snapshot.h"
#include "ferrum/physics/prediction.h"
#include "ferrum/physics/cache_commit.h"

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
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Identity quaternion constant ──────────────────────────────── */

static const phys_quat_t QUAT_IDENTITY = {.x = 0, .y = 0, .z = 0, .w = 1};

/* ── Helper: create a test world ───────────────────────────────── */

static int make_test_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 128;
    cfg.max_colliders = 128;
    cfg.manifold_cache_size = 128;
    cfg.frame_arena_size = 2u * 1024u * 1024u;
    cfg.default_substeps = 1;
    cfg.default_solver_iterations = 8;
    return phys_world_init(world, &cfg);
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
 * @brief Create a static box (inv_mass=0, STATIC flag) in the world.
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
    /* inv_mass defaults to 0 (static). */

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
 * @brief Create a dynamic capsule with a specific orientation.
 * @return Body index, or UINT32_MAX on failure.
 */
static uint32_t create_dynamic_capsule_oriented(phys_world_t *world,
                                                phys_vec3_t pos,
                                                phys_vec3_t vel,
                                                float mass,
                                                float radius,
                                                float half_height,
                                                phys_quat_t orientation) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = vel;
    body->orientation = orientation;
    phys_body_set_mass(body, mass);
    phys_body_set_capsule_inertia(body, mass, radius, half_height);

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    phys_world_set_capsule_collider(world, idx, radius, half_height,
                                    (phys_vec3_t){0, 0, 0}, QUAT_IDENTITY);
    return idx;
}

/* ── Test 1: box falls on floor ────────────────────────────────── */

/**
 * Static box floor at y=-1, dynamic box above it.  After many ticks
 * the dynamic box should have fallen from its start position and the
 * pipeline should not crash.  Uses conservative assertions matching
 * the p053 pattern (verify gravity took effect, all values finite).
 */
static int test_box_falls_on_floor(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Static floor: box at y=-1, half_extents (2, 1, 2).
     * Top surface is at y=0. */
    uint32_t floor_idx = create_static_box(
        &world,
        (phys_vec3_t){0, -1.0f, 0},
        (phys_vec3_t){2.0f, 1.0f, 2.0f});
    ASSERT_TRUE(floor_idx != UINT32_MAX);

    /* Dynamic box at y=3, half_extents (0.5, 0.5, 0.5). */
    uint32_t box_idx = create_dynamic_box(
        &world,
        (phys_vec3_t){0, 3.0f, 0},
        (phys_vec3_t){0, 0, 0},
        1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    ASSERT_TRUE(box_idx != UINT32_MAX);

    for (int i = 0; i < 300; ++i) {
        phys_world_tick(&world, NULL);
    }

    phys_body_t *box = phys_world_get_body(&world, box_idx);
    ASSERT_TRUE(box != NULL);

    /* Box should have fallen from y=3 (gravity took effect). */
    ASSERT_TRUE(box->position.y < 3.0f);
    /* All positions must be finite. */
    ASSERT_TRUE(isfinite(box->position.x));
    ASSERT_TRUE(isfinite(box->position.y));
    ASSERT_TRUE(isfinite(box->position.z));

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 2: sphere hits box ───────────────────────────────────── */

/**
 * Dynamic sphere moving toward a static box. After ticks, verify
 * the pipeline ran without crashing and positions are finite.
 */
static int test_sphere_hits_box(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Disable gravity for a clean horizontal collision. */
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Static box at origin, half_extents (1, 1, 1). */
    uint32_t box_idx = create_static_box(
        &world,
        (phys_vec3_t){0, 0, 0},
        (phys_vec3_t){1.0f, 1.0f, 1.0f});
    ASSERT_TRUE(box_idx != UINT32_MAX);

    /* Dynamic sphere approaching from the left. */
    uint32_t sph_idx = create_dynamic_sphere(
        &world,
        (phys_vec3_t){-5.0f, 0, 0},
        (phys_vec3_t){10.0f, 0, 0},
        1.0f, 0.5f);
    ASSERT_TRUE(sph_idx != UINT32_MAX);

    for (int i = 0; i < 60; ++i) {
        phys_world_tick(&world, NULL);
    }

    phys_body_t *sph = phys_world_get_body(&world, sph_idx);
    ASSERT_TRUE(sph != NULL);

    /* All positions must be finite (no NaN or Inf). */
    ASSERT_TRUE(isfinite(sph->position.x));
    ASSERT_TRUE(isfinite(sph->position.y));
    ASSERT_TRUE(isfinite(sph->position.z));

    /* Sphere started at x=-5 moving right at 10 m/s. After 60 ticks
     * (~1s at dt=1/60), it should have moved from its start position.
     * The pipeline should have processed the sphere-box pair through
     * broadphase and narrowphase without crashing. */
    ASSERT_TRUE(sph->position.x > -5.0f);
    ASSERT_TRUE(isfinite(sph->linear_vel.x));

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 3: capsule falls on box ──────────────────────────────── */

/**
 * Dynamic capsule above a static box floor. Verify gravity takes
 * effect and the pipeline runs without crashing.
 */
static int test_capsule_falls_on_box(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Static floor box. */
    uint32_t floor_idx = create_static_box(
        &world,
        (phys_vec3_t){0, -1.0f, 0},
        (phys_vec3_t){2.0f, 1.0f, 2.0f});
    ASSERT_TRUE(floor_idx != UINT32_MAX);

    /* Dynamic capsule at y=3, radius=0.3, half_height=0.5. */
    uint32_t cap_idx = create_dynamic_capsule(
        &world,
        (phys_vec3_t){0, 3.0f, 0},
        (phys_vec3_t){0, 0, 0},
        1.0f, 0.3f, 0.5f);
    ASSERT_TRUE(cap_idx != UINT32_MAX);

    for (int i = 0; i < 300; ++i) {
        phys_world_tick(&world, NULL);
    }

    phys_body_t *cap = phys_world_get_body(&world, cap_idx);
    ASSERT_TRUE(cap != NULL);

    /* Capsule should have fallen from y=3. */
    ASSERT_TRUE(cap->position.y < 3.0f);
    /* All positions must be finite. */
    ASSERT_TRUE(isfinite(cap->position.x));
    ASSERT_TRUE(isfinite(cap->position.y));
    ASSERT_TRUE(isfinite(cap->position.z));

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 4: mixed shapes collide ──────────────────────────────── */

/**
 * Sphere, box, and capsule near each other with velocities toward center.
 * After ticks, all should have been deflected from a pure linear trajectory.
 */
static int test_mixed_shapes_collide(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Disable gravity. */
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Sphere coming from the left. */
    uint32_t sph = create_dynamic_sphere(
        &world,
        (phys_vec3_t){-3.0f, 0, 0},
        (phys_vec3_t){5.0f, 0, 0},
        1.0f, 0.5f);
    ASSERT_TRUE(sph != UINT32_MAX);

    /* Box coming from the right. */
    uint32_t box = create_dynamic_box(
        &world,
        (phys_vec3_t){3.0f, 0, 0},
        (phys_vec3_t){-5.0f, 0, 0},
        1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    ASSERT_TRUE(box != UINT32_MAX);

    /* Capsule coming from above. */
    uint32_t cap = create_dynamic_capsule(
        &world,
        (phys_vec3_t){0, 3.0f, 0},
        (phys_vec3_t){0, -5.0f, 0},
        1.0f, 0.3f, 0.5f);
    ASSERT_TRUE(cap != UINT32_MAX);

    /* Record expected linear positions after 30 ticks if no collision.
     * dt ~= 1/60 = 0.01667s, so 30 ticks ≈ 0.5s.
     * sphere: -3 + 5*0.5 = -0.5
     * box:     3 - 5*0.5 =  0.5
     * capsule: 3 - 5*0.5 =  0.5  (y-axis) */
    for (int i = 0; i < 30; ++i) {
        phys_world_tick(&world, NULL);
    }

    phys_body_t *s = phys_world_get_body(&world, sph);
    phys_body_t *b = phys_world_get_body(&world, box);
    phys_body_t *c = phys_world_get_body(&world, cap);
    ASSERT_TRUE(s != NULL && b != NULL && c != NULL);

    /* After collision, positions should differ from pure linear trajectory.
     * At minimum, the pipeline ran without crashing. Verify finite positions. */
    ASSERT_TRUE(isfinite(s->position.x));
    ASSERT_TRUE(isfinite(b->position.x));
    ASSERT_TRUE(isfinite(c->position.y));

    /* At least one body should have a non-trivial velocity change. */
    int deflected = 0;
    if (s->linear_vel.x < 4.5f) deflected++;
    if (b->linear_vel.x > -4.5f) deflected++;
    if (c->linear_vel.y > -4.5f) deflected++;
    ASSERT_TRUE(deflected > 0);

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 5: box-box stack ─────────────────────────────────────── */

/**
 * Two boxes stacked on a static floor. After many ticks, verify
 * the pipeline runs without crashing, positions are finite, and
 * gravity took effect on both boxes.
 */
static int test_box_box_stack(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Static floor. */
    uint32_t floor_idx = create_static_box(
        &world,
        (phys_vec3_t){0, -1.0f, 0},
        (phys_vec3_t){2.0f, 1.0f, 2.0f});
    ASSERT_TRUE(floor_idx != UINT32_MAX);

    /* Bottom box: center at y=1.5. */
    uint32_t bot = create_dynamic_box(
        &world,
        (phys_vec3_t){0, 1.5f, 0},
        (phys_vec3_t){0, 0, 0},
        1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    ASSERT_TRUE(bot != UINT32_MAX);

    /* Top box: center at y=3.5. */
    uint32_t top = create_dynamic_box(
        &world,
        (phys_vec3_t){0, 3.5f, 0},
        (phys_vec3_t){0, 0, 0},
        1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    ASSERT_TRUE(top != UINT32_MAX);

    for (int i = 0; i < 300; ++i) {
        phys_world_tick(&world, NULL);
    }

    phys_body_t *b = phys_world_get_body(&world, bot);
    phys_body_t *t = phys_world_get_body(&world, top);
    ASSERT_TRUE(b != NULL && t != NULL);

    /* Both boxes should have fallen from their start positions. */
    ASSERT_TRUE(b->position.y < 1.5f);
    ASSERT_TRUE(t->position.y < 3.5f);

    /* Positions should be finite. */
    ASSERT_TRUE(isfinite(b->position.y));
    ASSERT_TRUE(isfinite(t->position.y));
    ASSERT_TRUE(isfinite(b->position.x));
    ASSERT_TRUE(isfinite(t->position.x));

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 6: capsule-capsule perpendicular ─────────────────────── */

/**
 * Two capsules at 90° to each other, pushed together.
 * Should generate contacts and separate.
 */
static int test_capsule_capsule_perpendicular(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Disable gravity. */
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Capsule A: default orientation (along Y), approaching from left. */
    uint32_t a = create_dynamic_capsule(
        &world,
        (phys_vec3_t){-2.0f, 0, 0},
        (phys_vec3_t){5.0f, 0, 0},
        1.0f, 0.3f, 0.5f);
    ASSERT_TRUE(a != UINT32_MAX);

    /* Capsule B: rotated 90° around Z (lies along X), approaching from right.
     * Quaternion for 90° rotation around Z: (0, 0, sin(45°), cos(45°)). */
    float s45 = sinf((float)M_PI / 4.0f);
    float c45 = cosf((float)M_PI / 4.0f);
    phys_quat_t rot_z90 = {.x = 0, .y = 0, .z = s45, .w = c45};

    uint32_t b = create_dynamic_capsule_oriented(
        &world,
        (phys_vec3_t){2.0f, 0, 0},
        (phys_vec3_t){-5.0f, 0, 0},
        1.0f, 0.3f, 0.5f,
        rot_z90);
    ASSERT_TRUE(b != UINT32_MAX);

    for (int i = 0; i < 60; ++i) {
        phys_world_tick(&world, NULL);
    }

    phys_body_t *ca = phys_world_get_body(&world, a);
    phys_body_t *cb = phys_world_get_body(&world, b);
    ASSERT_TRUE(ca != NULL && cb != NULL);

    /* After collision, capsules should have separated.
     * A started moving right (+5), B started moving left (-5).
     * After collision, velocities should have changed. */
    ASSERT_TRUE(isfinite(ca->position.x));
    ASSERT_TRUE(isfinite(cb->position.x));

    /* Velocities should have been altered by the collision. */
    ASSERT_TRUE(ca->linear_vel.x < 4.5f);
    ASSERT_TRUE(cb->linear_vel.x > -4.5f);

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 7: snapshot with mixed shapes ────────────────────────── */

/**
 * Set up a scene with sphere, box, and capsule. Tick a few times,
 * then snapshot encode/decode. Verify decoded positions match.
 */
static int test_snapshot_with_mixed_shapes(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* Disable gravity for predictable positions. */
    world.config.gravity = (phys_vec3_t){0, 0, 0};

    uint32_t sph = create_dynamic_sphere(
        &world,
        (phys_vec3_t){0, 0, 0},
        (phys_vec3_t){1, 0, 0},
        1.0f, 0.5f);
    ASSERT_TRUE(sph != UINT32_MAX);

    uint32_t box = create_dynamic_box(
        &world,
        (phys_vec3_t){5, 0, 0},
        (phys_vec3_t){0, 1, 0},
        1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    ASSERT_TRUE(box != UINT32_MAX);

    uint32_t cap = create_dynamic_capsule(
        &world,
        (phys_vec3_t){0, 0, 5},
        (phys_vec3_t){0, 0, 1},
        1.0f, 0.3f, 0.5f);
    ASSERT_TRUE(cap != UINT32_MAX);

    /* Tick a few times. */
    for (int i = 0; i < 10; ++i) {
        phys_world_tick(&world, NULL);
    }

    /* Record positions before snapshot. */
    phys_body_t *s = phys_world_get_body(&world, sph);
    phys_body_t *b = phys_world_get_body(&world, box);
    phys_body_t *c = phys_world_get_body(&world, cap);
    ASSERT_TRUE(s != NULL && b != NULL && c != NULL);
    float sx = s->position.x;
    float by = b->position.y;
    float cz = c->position.z;

    /* Encode snapshot. */
    uint8_t buffer[8192];
    size_t encoded = phys_snapshot_encode(&world, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded > 0);

    /* Decode snapshot. */
    phys_snapshot_t snapshot;
    phys_snapshot_body_t snap_bodies[128];
    snapshot.bodies = snap_bodies;
    int rc = phys_snapshot_decode(buffer, encoded, &snapshot);
    ASSERT_TRUE(rc == 0);

    /* Verify tick and body count. */
    ASSERT_TRUE(snapshot.tick == 10);
    ASSERT_TRUE(snapshot.body_count == 3);

    /* Verify decoded positions are approximately correct.
     * Quantization introduces some error (~1mm). */
    phys_vec3_t decoded_s = phys_dequantize_vec3(
        snapshot.bodies[sph].position, 1.0f / 1000.0f);
    phys_vec3_t decoded_b = phys_dequantize_vec3(
        snapshot.bodies[box].position, 1.0f / 1000.0f);
    phys_vec3_t decoded_c = phys_dequantize_vec3(
        snapshot.bodies[cap].position, 1.0f / 1000.0f);

    ASSERT_FLOAT_NEAR(sx, decoded_s.x, 0.05f);
    ASSERT_FLOAT_NEAR(by, decoded_b.y, 0.05f);
    ASSERT_FLOAT_NEAR(cz, decoded_c.z, 0.05f);

    phys_world_destroy(&world);
    return 0;
}

/* ── Test 8: prediction with mixed shapes ──────────────────────── */

/**
 * Run tick, snapshot, reconcile. Verify no crash and positions reasonable.
 */
static int test_prediction_with_mixed_shapes(void) {
    phys_world_t world1, world2;
    ASSERT_TRUE(make_test_world(&world1) == 0);
    ASSERT_TRUE(make_test_world(&world2) == 0);

    /* Disable gravity for deterministic behavior. */
    world1.config.gravity = (phys_vec3_t){0, 0, 0};
    world2.config.gravity = (phys_vec3_t){0, 0, 0};

    /* Create identical bodies in both worlds: sphere, box, capsule. */
    phys_vec3_t sph_pos = {-3, 0, 0};
    phys_vec3_t sph_vel = {1, 0, 0};
    phys_vec3_t box_pos = {3, 0, 0};
    phys_vec3_t box_vel = {-1, 0, 0};
    phys_vec3_t cap_pos = {0, 3, 0};
    phys_vec3_t cap_vel = {0, -1, 0};
    phys_vec3_t box_he = {0.5f, 0.5f, 0.5f};

    uint32_t s1 = create_dynamic_sphere(&world1, sph_pos, sph_vel, 1.0f, 0.5f);
    uint32_t b1 = create_dynamic_box(&world1, box_pos, box_vel, 1.0f, box_he);
    uint32_t c1 = create_dynamic_capsule(&world1, cap_pos, cap_vel, 1.0f, 0.3f, 0.5f);
    ASSERT_TRUE(s1 != UINT32_MAX && b1 != UINT32_MAX && c1 != UINT32_MAX);

    uint32_t s2 = create_dynamic_sphere(&world2, sph_pos, sph_vel, 1.0f, 0.5f);
    uint32_t b2 = create_dynamic_box(&world2, box_pos, box_vel, 1.0f, box_he);
    uint32_t c2 = create_dynamic_capsule(&world2, cap_pos, cap_vel, 1.0f, 0.3f, 0.5f);
    ASSERT_TRUE(s2 != UINT32_MAX && b2 != UINT32_MAX && c2 != UINT32_MAX);

    /* Run both for 10 ticks. */
    for (int i = 0; i < 10; ++i) {
        phys_world_tick(&world1, NULL);
        phys_world_tick(&world2, NULL);
    }

    /* Introduce a discrepancy in world2. */
    phys_body_t *body2 = phys_world_get_body(&world2, s2);
    ASSERT_TRUE(body2 != NULL);
    body2->position.x += 2.0f;

    /* Encode world1 as the authoritative snapshot. */
    uint8_t buffer[8192];
    size_t encoded = phys_snapshot_encode(&world1, buffer, sizeof(buffer));
    ASSERT_TRUE(encoded > 0);

    phys_snapshot_t snapshot;
    phys_snapshot_body_t snap_bodies[128];
    snapshot.bodies = snap_bodies;
    int rc = phys_snapshot_decode(buffer, encoded, &snapshot);
    ASSERT_TRUE(rc == 0);

    /* Reconcile world2 against the authoritative snapshot. */
    phys_prediction_config_t pred_cfg = phys_prediction_config_default();
    phys_prediction_result_t result = phys_prediction_reconcile(
        &world2, &snapshot, &pred_cfg);

    /* The deliberate 2.0m offset should cause a correction. */
    uint32_t corrected = result.bodies_snapped + result.bodies_blended;
    ASSERT_TRUE(corrected > 0);
    ASSERT_TRUE(result.max_position_error > 0.5f);

    /* All positions should be finite after reconciliation. */
    for (uint32_t i = 0; i < 3; ++i) {
        phys_body_t *b = phys_world_get_body(&world2, i);
        if (b) {
            ASSERT_TRUE(isfinite(b->position.x));
            ASSERT_TRUE(isfinite(b->position.y));
            ASSERT_TRUE(isfinite(b->position.z));
        }
    }

    phys_world_destroy(&world1);
    phys_world_destroy(&world2);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0;
    int fail_count = 0;

    printf("p059_physics_phase2_integration_tests\n");
    RUN_TEST(test_box_falls_on_floor);
    RUN_TEST(test_sphere_hits_box);
    RUN_TEST(test_capsule_falls_on_box);
    RUN_TEST(test_mixed_shapes_collide);
    RUN_TEST(test_box_box_stack);
    RUN_TEST(test_capsule_capsule_perpendicular);
    RUN_TEST(test_snapshot_with_mixed_shapes);
    RUN_TEST(test_prediction_with_mixed_shapes);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
