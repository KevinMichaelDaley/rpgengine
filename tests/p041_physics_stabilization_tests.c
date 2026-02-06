/**
 * @file p041_physics_stabilization_tests.c
 * @brief Unit tests for Stage 8: Stabilization Hints.
 *
 * Tests cover: resting contacts, approaching contacts, separating
 * contacts, sliding contacts, multiple manifolds, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/stabilization.h"

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

/* ── Helpers ────────────────────────────────────────────────────── */

/** Create a body at the given position with the given velocities. */
static phys_body_t make_body(float px, float py, float pz,
                             float vx, float vy, float vz,
                             float wx, float wy, float wz)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position    = (phys_vec3_t){px, py, pz};
    b.linear_vel  = (phys_vec3_t){vx, vy, vz};
    b.angular_vel = (phys_vec3_t){wx, wy, wz};
    b.inv_mass    = 1.0f;
    b.flags       = 0; /* dynamic */
    return b;
}

/** Create a manifold between body_a and body_b with one contact point. */
static phys_manifold_t make_manifold_one_point(uint32_t body_a,
                                                uint32_t body_b,
                                                phys_vec3_t point_world,
                                                phys_vec3_t normal)
{
    phys_manifold_t m;
    memset(&m, 0, sizeof(m));
    m.body_a      = body_a;
    m.body_b      = body_b;
    m.point_count = 1;
    m.points[0].point_world = point_world;
    m.points[0].normal      = normal;
    m.points[0].penetration = 0.01f;
    return m;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Two bodies at rest (near-zero velocity). Contact normal
 * pointing up (+Y). Both velocities tiny. Should produce resting
 * hints: friction_scale=3.0, restitution_scale=0.0.
 */
static int test_stab_resting(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_body(0.0f, 1.0f, 0.0f,
                          0.0f, 0.001f, 0.0f,   /* tiny linear velocity */
                          0.0f, 0.0f, 0.0f);
    bodies[1] = make_body(0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    phys_vec3_t contact_pt = {0.0f, 0.5f, 0.0f};
    phys_vec3_t normal     = {0.0f, 1.0f, 0.0f};
    phys_manifold_t manifold = make_manifold_one_point(0, 1, contact_pt, normal);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    phys_stabilization_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds                  = &manifold;
    args.manifold_count             = 1;
    args.bodies                     = bodies;
    args.hints_out                  = &hint;
    args.resting_velocity_threshold = 0.01f;

    phys_stage_stabilization(&args);

    ASSERT_FLOAT_NEAR(3.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, hint.restitution_scale, 1e-5f);

    return 0;
}

/**
 * Test 2: Body A falling fast toward body B. Normal component of
 * relative velocity exceeds threshold. Should produce active hints:
 * friction_scale=1.0, restitution_scale=1.0.
 */
static int test_stab_approaching(void)
{
    phys_body_t bodies[2];
    /* Body A falling downward at -5 m/s */
    bodies[0] = make_body(0.0f, 2.0f, 0.0f,
                          0.0f, -5.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
    /* Body B stationary */
    bodies[1] = make_body(0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    phys_vec3_t contact_pt = {0.0f, 1.0f, 0.0f};
    phys_vec3_t normal     = {0.0f, 1.0f, 0.0f};
    phys_manifold_t manifold = make_manifold_one_point(0, 1, contact_pt, normal);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    phys_stabilization_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds                  = &manifold;
    args.manifold_count             = 1;
    args.bodies                     = bodies;
    args.hints_out                  = &hint;
    args.resting_velocity_threshold = 0.1f;

    phys_stage_stabilization(&args);

    ASSERT_FLOAT_NEAR(1.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, hint.restitution_scale, 1e-5f);

    return 0;
}

/**
 * Test 3: Body A moving away from body B (positive v_n). Should
 * produce active hints: friction_scale=1.0, restitution_scale=1.0.
 */
static int test_stab_separating(void)
{
    phys_body_t bodies[2];
    /* Body A moving upward at +3 m/s */
    bodies[0] = make_body(0.0f, 1.0f, 0.0f,
                          0.0f, 3.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
    /* Body B stationary */
    bodies[1] = make_body(0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    phys_vec3_t contact_pt = {0.0f, 0.5f, 0.0f};
    phys_vec3_t normal     = {0.0f, 1.0f, 0.0f};
    phys_manifold_t manifold = make_manifold_one_point(0, 1, contact_pt, normal);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    phys_stabilization_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds                  = &manifold;
    args.manifold_count             = 1;
    args.bodies                     = bodies;
    args.hints_out                  = &hint;
    args.resting_velocity_threshold = 0.1f;

    phys_stage_stabilization(&args);

    ASSERT_FLOAT_NEAR(1.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, hint.restitution_scale, 1e-5f);

    return 0;
}

/**
 * Test 4: Body A sliding tangentially (zero normal velocity, but
 * tangential speed above threshold). Should produce active hints.
 */
static int test_stab_sliding(void)
{
    phys_body_t bodies[2];
    /* Body A sliding along X at 2.0 m/s, zero Y velocity */
    bodies[0] = make_body(0.0f, 1.0f, 0.0f,
                          2.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
    /* Body B stationary */
    bodies[1] = make_body(0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    /* Contact at center, normal pointing up */
    phys_vec3_t contact_pt = {0.0f, 0.5f, 0.0f};
    phys_vec3_t normal     = {0.0f, 1.0f, 0.0f};
    phys_manifold_t manifold = make_manifold_one_point(0, 1, contact_pt, normal);

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));

    phys_stabilization_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds                  = &manifold;
    args.manifold_count             = 1;
    args.bodies                     = bodies;
    args.hints_out                  = &hint;
    args.resting_velocity_threshold = 0.1f;

    phys_stage_stabilization(&args);

    /* Tangential speed exceeds threshold → active */
    ASSERT_FLOAT_NEAR(1.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, hint.restitution_scale, 1e-5f);

    return 0;
}

/**
 * Test 5: Three manifolds with different velocity configurations.
 * Manifold 0: resting, Manifold 1: approaching (active),
 * Manifold 2: resting.
 */
static int test_stab_multiple_manifolds(void)
{
    phys_body_t bodies[6];

    /* Pair 0-1: resting (tiny velocities) */
    bodies[0] = make_body(0.0f, 1.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
    bodies[1] = make_body(0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    /* Pair 2-3: approaching (fast downward) */
    bodies[2] = make_body(5.0f, 3.0f, 0.0f,
                          0.0f, -10.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
    bodies[3] = make_body(5.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    /* Pair 4-5: resting (both still) */
    bodies[4] = make_body(10.0f, 1.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);
    bodies[5] = make_body(10.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f,
                          0.0f, 0.0f, 0.0f);

    phys_vec3_t normal = {0.0f, 1.0f, 0.0f};

    phys_manifold_t manifolds[3];
    manifolds[0] = make_manifold_one_point(0, 1,
        (phys_vec3_t){0.0f, 0.5f, 0.0f}, normal);
    manifolds[1] = make_manifold_one_point(2, 3,
        (phys_vec3_t){5.0f, 1.5f, 0.0f}, normal);
    manifolds[2] = make_manifold_one_point(4, 5,
        (phys_vec3_t){10.0f, 0.5f, 0.0f}, normal);

    phys_stab_hint_t hints[3];
    memset(hints, 0, sizeof(hints));

    phys_stabilization_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds                  = manifolds;
    args.manifold_count             = 3;
    args.bodies                     = bodies;
    args.hints_out                  = hints;
    args.resting_velocity_threshold = 0.1f;

    phys_stage_stabilization(&args);

    /* Manifold 0: resting */
    ASSERT_FLOAT_NEAR(3.0f, hints[0].friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, hints[0].restitution_scale, 1e-5f);

    /* Manifold 1: approaching (active) */
    ASSERT_FLOAT_NEAR(1.0f, hints[1].friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, hints[1].restitution_scale, 1e-5f);

    /* Manifold 2: resting */
    ASSERT_FLOAT_NEAR(3.0f, hints[2].friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, hints[2].restitution_scale, 1e-5f);

    return 0;
}

/**
 * Test 6: NULL args doesn't crash.
 */
static int test_stab_null_safe(void)
{
    /* NULL args — should be a safe no-op. */
    phys_stage_stabilization(NULL);

    /* Args with NULL internals — should not crash. */
    phys_stabilization_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_stabilization(&args);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p041_physics_stabilization_tests\n");

    RUN_TEST(test_stab_resting);
    RUN_TEST(test_stab_approaching);
    RUN_TEST(test_stab_separating);
    RUN_TEST(test_stab_sliding);
    RUN_TEST(test_stab_multiple_manifolds);
    RUN_TEST(test_stab_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
