/**
 * @file p076_physics_tier_stabilization_tests.c
 * @brief Unit tests for per-tier stabilization scaling.
 *
 * Tests verify that stabilization hints (friction_boost, velocity_damping)
 * scale correctly with simulation tier, and that cross-tier pairs use the
 * higher (lower-fidelity) tier's parameters.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/tier_list.h"

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

/** Create a resting body at the given position with a specific tier. */
static phys_body_t make_tiered_body(float px, float py, float pz,
                                    uint8_t tier)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position    = (phys_vec3_t){px, py, pz};
    b.linear_vel  = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.inv_mass    = 1.0f;
    b.flags       = 0; /* dynamic */
    b.tier        = tier;
    return b;
}

/** Create a manifold between body_a and body_b with one resting contact. */
static phys_manifold_t make_resting_manifold(uint32_t body_a, uint32_t body_b)
{
    phys_manifold_t m;
    memset(&m, 0, sizeof(m));
    m.body_a      = body_a;
    m.body_b      = body_b;
    m.point_count = 1;
    m.points[0].point_world = (phys_vec3_t){0.0f, 0.5f, 0.0f};
    m.points[0].normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    m.points[0].penetration = 0.01f;
    return m;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: T0 body pair gets the strongest stabilization.
 * friction_boost=3.0, velocity_damping=0.98
 */
static int test_t0_strongest_stabilization(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_tiered_body(0.0f, 1.0f, 0.0f, PHYS_TIER_0_DIRECT);
    bodies[1] = make_tiered_body(0.0f, 0.0f, 0.0f, PHYS_TIER_0_DIRECT);

    phys_manifold_t manifold = make_resting_manifold(0, 1);

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

    /* T0 resting: friction_boost=3.0, velocity_damping=0.98 */
    ASSERT_FLOAT_NEAR(3.0f, hint.friction_boost, 1e-5f);
    ASSERT_FLOAT_NEAR(0.98f, hint.velocity_damping, 1e-5f);

    return 0;
}

/**
 * Test 2: T1 body pair gets moderate stabilization.
 * friction_boost=2.0, velocity_damping=0.97
 */
static int test_t1_stabilization(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_tiered_body(0.0f, 1.0f, 0.0f, PHYS_TIER_1_NEAR);
    bodies[1] = make_tiered_body(0.0f, 0.0f, 0.0f, PHYS_TIER_1_NEAR);

    phys_manifold_t manifold = make_resting_manifold(0, 1);

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

    ASSERT_FLOAT_NEAR(2.0f, hint.friction_boost, 1e-5f);
    ASSERT_FLOAT_NEAR(0.97f, hint.velocity_damping, 1e-5f);

    return 0;
}

/**
 * Test 3: T4 body pair gets minimal stabilization.
 * friction_boost=1.0, velocity_damping=0.85
 */
static int test_t4_minimal_stabilization(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_tiered_body(0.0f, 1.0f, 0.0f, PHYS_TIER_4_BACKGROUND);
    bodies[1] = make_tiered_body(0.0f, 0.0f, 0.0f, PHYS_TIER_4_BACKGROUND);

    phys_manifold_t manifold = make_resting_manifold(0, 1);

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

    ASSERT_FLOAT_NEAR(1.0f, hint.friction_boost, 1e-5f);
    ASSERT_FLOAT_NEAR(0.85f, hint.velocity_damping, 1e-5f);

    return 0;
}

/**
 * Test 4: Cross-tier pair (T0 + T3) uses the higher tier (T3 = lower
 * fidelity, more conservative) for stabilization parameters.
 * T3: friction_boost=1.0, velocity_damping=0.90
 */
static int test_cross_tier_uses_lower_fidelity(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_tiered_body(0.0f, 1.0f, 0.0f, PHYS_TIER_0_DIRECT);
    bodies[1] = make_tiered_body(0.0f, 0.0f, 0.0f, PHYS_TIER_3_WORLD);

    phys_manifold_t manifold = make_resting_manifold(0, 1);

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

    /* Should use T3 params (higher tier = lower fidelity). */
    ASSERT_FLOAT_NEAR(1.0f, hint.friction_boost, 1e-5f);
    ASSERT_FLOAT_NEAR(0.90f, hint.velocity_damping, 1e-5f);

    return 0;
}

/**
 * Test 5: Verify that resting contact friction_scale is also affected
 * by the tier friction_boost. For resting contacts, the base
 * friction_scale is 3.0; the tier friction_boost multiplies it.
 * T0: friction_scale should be 3.0 * 3.0 = 9.0
 * T2: friction_scale should be 3.0 * 1.5 = 4.5
 */
static int test_resting_contact_with_friction_boost(void)
{
    /* T2 pair — resting contact */
    phys_body_t bodies[2];
    bodies[0] = make_tiered_body(0.0f, 1.0f, 0.0f, PHYS_TIER_2_VISIBLE);
    bodies[1] = make_tiered_body(0.0f, 0.0f, 0.0f, PHYS_TIER_2_VISIBLE);

    phys_manifold_t manifold = make_resting_manifold(0, 1);

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

    /* T2: friction_boost=1.5.
     * Resting friction_scale = 3.0 * friction_boost = 4.5 */
    ASSERT_FLOAT_NEAR(4.5f, hint.friction_scale, 1e-5f);

    /* T2: velocity_damping=0.95 */
    ASSERT_FLOAT_NEAR(0.95f, hint.velocity_damping, 1e-5f);
    ASSERT_FLOAT_NEAR(1.5f, hint.friction_boost, 1e-5f);

    return 0;
}

/**
 * Test 6: Bodies with tier=0 (Phase 1 default from phys_body_init)
 * still produce the same behavior as before — backward compatibility.
 * Default tier is 0 (PHYS_TIER_0_DIRECT), so resting contacts should
 * get friction_scale = 3.0 * 3.0 = 9.0 (boosted), restitution_scale = 0.0.
 */
static int test_stabilization_backward_compatible(void)
{
    /* Use phys_body_init defaults — tier should be 0. */
    phys_body_t bodies[2];
    phys_body_init(&bodies[0]);
    bodies[0].position    = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    bodies[0].inv_mass    = 1.0f;
    bodies[0].flags       = 0;

    phys_body_init(&bodies[1]);
    bodies[1].position    = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    bodies[1].inv_mass    = 1.0f;
    bodies[1].flags       = 0;

    /* Verify default tier is 0. */
    ASSERT_TRUE(bodies[0].tier == 0);
    ASSERT_TRUE(bodies[1].tier == 0);

    phys_manifold_t manifold = make_resting_manifold(0, 1);

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

    /* T0 resting: friction_scale = 3.0 * 3.0 = 9.0, restitution_scale = 0.0 */
    ASSERT_FLOAT_NEAR(9.0f, hint.friction_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, hint.restitution_scale, 1e-5f);
    ASSERT_FLOAT_NEAR(3.0f, hint.friction_boost, 1e-5f);
    ASSERT_FLOAT_NEAR(0.98f, hint.velocity_damping, 1e-5f);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p076_physics_tier_stabilization_tests\n");

    RUN_TEST(test_t0_strongest_stabilization);
    RUN_TEST(test_t1_stabilization);
    RUN_TEST(test_t4_minimal_stabilization);
    RUN_TEST(test_cross_tier_uses_lower_fidelity);
    RUN_TEST(test_resting_contact_with_friction_boost);
    RUN_TEST(test_stabilization_backward_compatible);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
