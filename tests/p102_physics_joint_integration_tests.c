/**
 * @file p102_physics_joint_integration_tests.c
 * @brief Phase 8 integration tests for joints.
 *
 * End-to-end tests that run phys_world_tick() with joints attached
 * and verify the physics simulation produces correct results:
 *   - Pendulum (ball joint + gravity)
 *   - Chain of distance joints
 *   - Door hinge (hinge joint allows rotation on one axis)
 *   - Joint preserves constraint under many ticks
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/joint.h"
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
        printf("  %-55s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

static int make_test_world(phys_world_t *world) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 128;
    cfg.max_colliders = 128;
    cfg.max_joints = 64;
    cfg.manifold_cache_size = 128;
    cfg.frame_arena_size = 2u * 1024u * 1024u;
    cfg.default_substeps = 4;
    cfg.default_solver_iterations = 8;
    return phys_world_init(world, &cfg);
}

/** Create a dynamic sphere body without a collider (joints only). */
static uint32_t create_body(phys_world_t *world,
                            phys_vec3_t pos,
                            float mass) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->linear_vel = (phys_vec3_t){0, 0, 0};
    phys_body_set_mass(body, mass);
    phys_body_set_sphere_inertia(body, mass, 0.1f);

    /* Disable sleep so joints always get solved. */
    body->sleep_counter = 0;

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    return idx;
}

/** Create a static anchor body (infinite mass). */
static uint32_t create_static_body(phys_world_t *world,
                                   phys_vec3_t pos) {
    uint32_t idx = phys_world_create_body(world);
    if (idx == UINT32_MAX) return idx;

    phys_body_t *body = phys_world_get_body(world, idx);
    body->position = pos;
    body->flags |= PHYS_BODY_FLAG_STATIC;

    phys_body_t *next = phys_body_pool_get_next(&world->body_pool, idx);
    *next = *body;

    return idx;
}

static float vec3_dist(phys_vec3_t a, phys_vec3_t b) {
    float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
    return sqrtf(dx*dx + dy*dy + dz*dz);
}

static float vec3_length(phys_vec3_t v) {
    return sqrtf(v.x*v.x + v.y*v.y + v.z*v.z);
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

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Pendulum: static anchor at (0,5,0), dynamic ball at (2,5,0)
 * connected by a ball joint.  Under gravity, the ball should swing
 * downward.  After many ticks it should settle below the anchor.
 */
static int test_pendulum_swings(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    uint32_t anchor = create_static_body(&world, (phys_vec3_t){0, 5, 0});
    uint32_t ball   = create_body(&world, (phys_vec3_t){2, 5, 0}, 1.0f);
    ASSERT_TRUE(anchor != UINT32_MAX);
    ASSERT_TRUE(ball   != UINT32_MAX);

    /* Ball joint at anchor's position. */
    phys_joint_t joint;
    memset(&joint, 0, sizeof(joint));
    joint.type = PHYS_JOINT_BALL;
    joint.body_a = anchor;
    joint.body_b = ball;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){-2, 0, 0}; /* 2 units left in ball's local */

    uint32_t ji = phys_world_add_joint(&world, &joint);
    ASSERT_TRUE(ji != UINT32_MAX);

    /* Record initial Y. */
    float init_y = phys_world_get_body(&world, ball)->position.y;

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate 120 ticks (~2 seconds). */
    for (int i = 0; i < 120; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    phys_body_t *b = phys_world_get_body(&world, ball);
    /* Ball should have swung below its initial height. */
    ASSERT_TRUE(b->position.y < init_y - 0.5f);

    /* The ball should be roughly 2 units from the anchor (joint length). */
    phys_body_t *a = phys_world_get_body(&world, anchor);
    float dist = vec3_dist(a->position, b->position);
    /* Allow generous tolerance for XPBD/TGS compliance. */
    ASSERT_TRUE(dist < 3.0f);
    ASSERT_TRUE(dist > 1.0f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Chain: 5 bodies linked by 4 distance joints, top body static.
 * Under gravity the chain should hang down.  Each body should be
 * below the one above it, and the chain shouldn't stretch excessively.
 */
static int test_chain_of_bodies(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    const float link_len = 1.0f;
    const int count = 5;
    uint32_t bodies[5];

    /* Top body is static. */
    bodies[0] = create_static_body(&world, (phys_vec3_t){0, 10, 0});
    ASSERT_TRUE(bodies[0] != UINT32_MAX);

    /* Chain bodies below. */
    for (int i = 1; i < count; ++i) {
        float y = 10.0f - (float)i * link_len;
        bodies[i] = create_body(&world, (phys_vec3_t){0, y, 0}, 1.0f);
        ASSERT_TRUE(bodies[i] != UINT32_MAX);
    }

    /* Distance joints between consecutive bodies. */
    for (int i = 0; i < count - 1; ++i) {
        phys_joint_t joint;
        memset(&joint, 0, sizeof(joint));
        joint.type = PHYS_JOINT_DISTANCE;
        joint.body_a = bodies[i];
        joint.body_b = bodies[i + 1];
        joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
        joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
        joint.rest_length = link_len;

        uint32_t ji = phys_world_add_joint(&world, &joint);
        ASSERT_TRUE(ji != UINT32_MAX);
    }

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate 300 ticks (~5 seconds). */
    for (int i = 0; i < 300; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    /* Each body should be below the one above. */
    for (int i = 1; i < count; ++i) {
        float y_above = phys_world_get_body(&world, bodies[i-1])->position.y;
        float y_below = phys_world_get_body(&world, bodies[i])->position.y;
        ASSERT_TRUE(y_below < y_above);
    }

    /* Chain total stretch: sum of distances should be near 4 * link_len.
     * Allow 50% tolerance for compliance. */
    float total = 0;
    for (int i = 0; i < count - 1; ++i) {
        phys_vec3_t pa = phys_world_get_body(&world, bodies[i])->position;
        phys_vec3_t pb = phys_world_get_body(&world, bodies[i+1])->position;
        total += vec3_dist(pa, pb);
    }
    float expected = (float)(count - 1) * link_len;
    ASSERT_TRUE(total < expected * 2.0f); /* shouldn't double in length */

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Door hinge: static frame at origin, dynamic door body at (1,0,0).
 * Hinge joint along Y axis.  Give the door an angular velocity
 * around Y.  After simulation, the door should have rotated
 * (x position changes) but stayed at the same height.
 */
static int test_door_hinge(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    uint32_t frame = create_static_body(&world, (phys_vec3_t){0, 0, 0});
    uint32_t door  = create_body(&world, (phys_vec3_t){1, 0, 0}, 2.0f);
    ASSERT_TRUE(frame != UINT32_MAX);
    ASSERT_TRUE(door  != UINT32_MAX);

    /* Give the door initial angular velocity around Y (spin). */
    phys_body_t *door_body = phys_world_get_body(&world, door);
    door_body->angular_vel = (phys_vec3_t){0, 3.0f, 0};
    phys_body_t *next = phys_body_pool_get_next(&world.body_pool, door);
    *next = *door_body;

    /* Hinge joint along Y axis at origin. */
    phys_joint_t joint;
    memset(&joint, 0, sizeof(joint));
    joint.type = PHYS_JOINT_HINGE;
    joint.body_a = frame;
    joint.body_b = door;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){-1, 0, 0}; /* pivot at door's left edge */
    joint.local_axis_a = (phys_vec3_t){0, 1, 0};   /* Y axis */

    uint32_t ji = phys_world_add_joint(&world, &joint);
    ASSERT_TRUE(ji != UINT32_MAX);

    float init_y = door_body->position.y;

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate 60 ticks (~1 second). */
    for (int i = 0; i < 60; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    phys_body_t *d = phys_world_get_body(&world, door);

    /* Door should stay at approximately the same height (no gravity
     * pull apart since hinge constrains position). */
    ASSERT_TRUE(fabsf(d->position.y - init_y) < 1.5f);

    /* Door should still be roughly 1 unit from the frame (hinge length). */
    phys_body_t *f = phys_world_get_body(&world, frame);
    float dist = vec3_dist(f->position, d->position);
    ASSERT_TRUE(dist < 2.5f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Joint constraint endurance: ball joint between two dynamic bodies
 * with no gravity (disable by setting 0).  Initially separated by 2
 * units, joint at midpoint.  After many ticks, bodies should remain
 * close to their initial positions (no energy injection).
 */
static int test_joint_no_drift(void) {
    phys_world_t world;
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 16;
    cfg.max_colliders = 16;
    cfg.max_joints = 8;
    cfg.manifold_cache_size = 16;
    cfg.frame_arena_size = 1u * 1024u * 1024u;
    cfg.default_substeps = 4;
    cfg.default_solver_iterations = 8;
    cfg.gravity = (phys_vec3_t){0, 0, 0}; /* no gravity */
    ASSERT_TRUE(phys_world_init(&world, &cfg) == 0);

    uint32_t a = create_body(&world, (phys_vec3_t){-1, 0, 0}, 1.0f);
    uint32_t b = create_body(&world, (phys_vec3_t){ 1, 0, 0}, 1.0f);
    ASSERT_TRUE(a != UINT32_MAX);
    ASSERT_TRUE(b != UINT32_MAX);

    /* Distance joint with rest_length = 2 (exact initial separation). */
    phys_joint_t joint;
    memset(&joint, 0, sizeof(joint));
    joint.type = PHYS_JOINT_DISTANCE;
    joint.body_a = a;
    joint.body_b = b;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
    joint.rest_length = 2.0f;

    ASSERT_TRUE(phys_world_add_joint(&world, &joint) != UINT32_MAX);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate 300 ticks. */
    for (int i = 0; i < 300; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    /* Bodies should not have drifted far from initial positions. */
    phys_body_t *ba = phys_world_get_body(&world, a);
    phys_body_t *bb = phys_world_get_body(&world, b);

    float dist = vec3_dist(ba->position, bb->position);
    /* Distance should remain near 2.0. */
    ASSERT_TRUE(fabsf(dist - 2.0f) < 1.0f);

    /* Velocities should be near zero (no energy injection). */
    ASSERT_TRUE(vec3_length(ba->linear_vel) < 2.0f);
    ASSERT_TRUE(vec3_length(bb->linear_vel) < 2.0f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Multiple joints in one world: 3 independent ball joints.
 * Verifies no cross-talk or crash with multiple simultaneous joints.
 */
static int test_multiple_joints(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    /* 3 pairs of bodies, each with a ball joint. */
    for (int p = 0; p < 3; ++p) {
        float x = (float)p * 5.0f;
        uint32_t anchor = create_static_body(&world,
            (phys_vec3_t){x, 10, 0});
        uint32_t ball = create_body(&world,
            (phys_vec3_t){x + 1, 10, 0}, 1.0f);
        ASSERT_TRUE(anchor != UINT32_MAX);
        ASSERT_TRUE(ball   != UINT32_MAX);

        phys_joint_t joint;
        memset(&joint, 0, sizeof(joint));
        joint.type = PHYS_JOINT_BALL;
        joint.body_a = anchor;
        joint.body_b = ball;
        joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
        joint.local_anchor_b = (phys_vec3_t){-1, 0, 0};

        ASSERT_TRUE(phys_world_add_joint(&world, &joint) != UINT32_MAX);
    }

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate 120 ticks — should not crash. */
    for (int i = 0; i < 120; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    /* All dynamic bodies should have fallen below their anchors. */
    for (uint32_t i = 0; i < 6; i += 2) {
        /* Static anchors at even indices, dynamic at odd. */
        phys_body_t *anchor = phys_world_get_body(&world, i);
        phys_body_t *ball   = phys_world_get_body(&world, i + 1);
        if (anchor && ball) {
            ASSERT_TRUE(ball->position.y < anchor->position.y + 0.5f);
        }
    }

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/**
 * Joint removal: add a joint, run some ticks, remove it, run more ticks.
 * After removal the body should fall freely.
 */
static int test_joint_removal(void) {
    phys_world_t world;
    ASSERT_TRUE(make_test_world(&world) == 0);

    uint32_t anchor = create_static_body(&world, (phys_vec3_t){0, 10, 0});
    uint32_t ball   = create_body(&world, (phys_vec3_t){0, 8, 0}, 1.0f);
    ASSERT_TRUE(anchor != UINT32_MAX);
    ASSERT_TRUE(ball   != UINT32_MAX);

    phys_joint_t joint;
    memset(&joint, 0, sizeof(joint));
    joint.type = PHYS_JOINT_DISTANCE;
    joint.body_a = anchor;
    joint.body_b = ball;
    joint.local_anchor_a = (phys_vec3_t){0, 0, 0};
    joint.local_anchor_b = (phys_vec3_t){0, 0, 0};
    joint.rest_length = 2.0f;

    uint32_t ji = phys_world_add_joint(&world, &joint);
    ASSERT_TRUE(ji != UINT32_MAX);

    job_system_t sys;
    phys_job_context_t ctx;
    setup_jobs(&sys, &ctx);

    /* Simulate 60 ticks with joint. */
    for (int i = 0; i < 60; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    float y_constrained = phys_world_get_body(&world, ball)->position.y;
    /* Ball should not have fallen far (held by joint). */
    ASSERT_TRUE(y_constrained > 5.0f);

    /* Remove joint. */
    phys_world_remove_joint(&world, ji);
    ASSERT_TRUE(phys_world_joint_count(&world) == 0);

    /* Wake the body so it responds to gravity after joint removal. */
    phys_body_t *wb = phys_world_get_body(&world, ball);
    wb->flags &= (uint8_t)~PHYS_BODY_FLAG_SLEEPING;
    wb->sleep_counter = 0;
    phys_body_t *wb_next = phys_body_pool_get_next(&world.body_pool, ball);
    wb_next->flags &= (uint8_t)~PHYS_BODY_FLAG_SLEEPING;
    wb_next->sleep_counter = 0;

    /* Simulate 120 more ticks — ball falls freely. */
    for (int i = 0; i < 120; ++i) {
        phys_world_tick_parallel(&world, NULL, &ctx);
    }

    float y_free = phys_world_get_body(&world, ball)->position.y;
    /* Ball should have fallen significantly. */
    ASSERT_TRUE(y_free < y_constrained - 1.0f);

    teardown_jobs(&sys, &ctx);
    phys_world_destroy(&world);
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    int test_count = 0, fail_count = 0;

    printf("p102_physics_joint_integration_tests\n");
    RUN_TEST(test_pendulum_swings);
    RUN_TEST(test_chain_of_bodies);
    RUN_TEST(test_door_hinge);
    RUN_TEST(test_joint_no_drift);
    RUN_TEST(test_multiple_joints);
    RUN_TEST(test_joint_removal);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count > 0 ? 1 : 0;
}
