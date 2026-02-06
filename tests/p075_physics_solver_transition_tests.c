/**
 * @file p075_physics_solver_transition_tests.c
 * @brief Unit tests for solver transition pipeline integration (phys-403).
 *
 * Tests cover: cross-tier solver mode resolution, batch transition apply
 * (TGS↔XPBD lambda conversion), no-change passthrough, and constraint
 * build stage solver_mode tagging.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/constraint_stage.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/solver_transition.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/step_plan.h"
#include "ferrum/physics/tier_list.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                  \
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

#define ASSERT_EQ(exp, act)                                                    \
    do {                                                                        \
        if ((exp) != (act)) {                                                   \
            fprintf(stderr, "ASSERT_EQ failed: %s:%d: expected %d got %d\n",   \
                    __FILE__, __LINE__, (int)(exp), (int)(act));                 \
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

/* ── Test 1: Both T0 → TGS ─────────────────────────────────────── */

static int test_cross_tier_mode_t0_t0(void)
{
    phys_solver_mode_t mode = phys_tier_cross_solver_mode(
        PHYS_TIER_0_DIRECT, PHYS_TIER_0_DIRECT);
    ASSERT_EQ(PHYS_SOLVER_TGS, mode);
    return 0;
}

/* ── Test 2: T1 and T2 → TGS (high-fidelity wins) ──────────────── */

static int test_cross_tier_mode_t1_t2(void)
{
    phys_solver_mode_t mode = phys_tier_cross_solver_mode(
        PHYS_TIER_1_NEAR, PHYS_TIER_2_VISIBLE);
    ASSERT_EQ(PHYS_SOLVER_TGS, mode);

    /* Symmetric: T2 × T1 should also be TGS. */
    mode = phys_tier_cross_solver_mode(
        PHYS_TIER_2_VISIBLE, PHYS_TIER_1_NEAR);
    ASSERT_EQ(PHYS_SOLVER_TGS, mode);
    return 0;
}

/* ── Test 3: Both T2+ → XPBD ───────────────────────────────────── */

static int test_cross_tier_mode_t2_t3(void)
{
    phys_solver_mode_t mode = phys_tier_cross_solver_mode(
        PHYS_TIER_2_VISIBLE, PHYS_TIER_3_WORLD);
    ASSERT_EQ(PHYS_SOLVER_XPBD, mode);

    /* Both T2. */
    mode = phys_tier_cross_solver_mode(
        PHYS_TIER_2_VISIBLE, PHYS_TIER_2_VISIBLE);
    ASSERT_EQ(PHYS_SOLVER_XPBD, mode);

    /* T3 × T4. */
    mode = phys_tier_cross_solver_mode(
        PHYS_TIER_3_WORLD, PHYS_TIER_4_BACKGROUND);
    ASSERT_EQ(PHYS_SOLVER_XPBD, mode);
    return 0;
}

/* ── Helpers for transition_apply tests ─────────────────────────── */

/** Build a constraint with one row at the given lambda and clamp range. */
static phys_constraint_t make_constraint(float lambda, float lmin, float lmax,
                                         uint8_t solver_mode)
{
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    c.row_count          = 1;
    c.solver_mode        = solver_mode;
    c.rows[0].lambda     = lambda;
    c.rows[0].lambda_min = lmin;
    c.rows[0].lambda_max = lmax;
    return c;
}

/* ── Test 4: transition apply TGS → XPBD ───────────────────────── */

static int test_transition_apply_tgs_to_xpbd(void)
{
    const float dt = 1.0f / 60.0f;
    phys_constraint_t c = make_constraint(10.0f, 0.0f, 1e6f,
                                          PHYS_SOLVER_XPBD);
    uint8_t prev = PHYS_SOLVER_TGS;

    phys_solver_transition_apply(&c, 1, &prev, dt);

    /* λ_xpbd = λ_impulse * dt = 10 * (1/60). */
    ASSERT_FLOAT_NEAR(10.0f * dt, c.rows[0].lambda, 1e-5f);
    return 0;
}

/* ── Test 5: transition apply XPBD → TGS ───────────────────────── */

static int test_transition_apply_xpbd_to_tgs(void)
{
    const float dt = 1.0f / 60.0f;
    /* Large lambda that will be clamped to lambda_max after division. */
    phys_constraint_t c = make_constraint(1000.0f, 0.0f, 100.0f,
                                          PHYS_SOLVER_TGS);
    uint8_t prev = PHYS_SOLVER_XPBD;

    phys_solver_transition_apply(&c, 1, &prev, dt);

    /* 1000 / (1/60) = 60000 → clamped to 100. */
    ASSERT_FLOAT_NEAR(100.0f, c.rows[0].lambda, 1e-5f);
    return 0;
}

/* ── Test 6: no mode change → lambda unchanged ──────────────────── */

static int test_transition_apply_no_change(void)
{
    const float dt = 1.0f / 60.0f;
    const float original = 42.0f;

    /* TGS → TGS: no conversion. */
    phys_constraint_t c1 = make_constraint(original, 0.0f, 1e6f,
                                           PHYS_SOLVER_TGS);
    uint8_t prev1 = PHYS_SOLVER_TGS;
    phys_solver_transition_apply(&c1, 1, &prev1, dt);
    ASSERT_FLOAT_NEAR(original, c1.rows[0].lambda, 1e-6f);

    /* XPBD → XPBD: no conversion. */
    phys_constraint_t c2 = make_constraint(original, 0.0f, 1e6f,
                                           PHYS_SOLVER_XPBD);
    uint8_t prev2 = PHYS_SOLVER_XPBD;
    phys_solver_transition_apply(&c2, 1, &prev2, dt);
    ASSERT_FLOAT_NEAR(original, c2.rows[0].lambda, 1e-6f);
    return 0;
}

/* ── Test 7: constraint build sets solver_mode ──────────────────── */

static int test_constraint_build_sets_solver_mode(void)
{
    /* Set up two bodies at different tiers. */
    phys_body_t bodies[2];
    memset(bodies, 0, sizeof(bodies));
    phys_body_init(&bodies[0]);
    phys_body_init(&bodies[1]);

    /* Body 0 = T1 (near), Body 1 = T3 (world). */
    bodies[0].tier = PHYS_TIER_1_NEAR;
    bodies[1].tier = PHYS_TIER_3_WORLD;

    /* Give them mass so effective mass is non-zero. */
    phys_body_set_mass(&bodies[0], 1.0f);
    phys_body_set_mass(&bodies[1], 1.0f);
    phys_body_set_box_inertia(&bodies[0], 1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});
    phys_body_set_box_inertia(&bodies[1], 1.0f,
        (phys_vec3_t){0.5f, 0.5f, 0.5f});

    /* Build a manifold with one contact point. */
    phys_manifold_t manifold;
    memset(&manifold, 0, sizeof(manifold));
    manifold.body_a      = 0;
    manifold.body_b      = 1;
    manifold.point_count = 1;
    manifold.friction    = 0.5f;
    manifold.restitution = 0.3f;
    manifold.points[0].normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
    manifold.points[0].penetration = 0.01f;

    phys_stab_hint_t hint;
    memset(&hint, 0, sizeof(hint));
    hint.friction_scale    = 1.0f;
    hint.restitution_scale = 1.0f;

    phys_constraint_t constraints[4];
    memset(constraints, 0, sizeof(constraints));
    uint32_t count = 0;

    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds            = &manifold;
    args.hints                = &hint;
    args.manifold_count       = 1;
    args.bodies               = bodies;
    args.constraints_out      = constraints;
    args.constraint_count_out = &count;
    args.max_constraints      = 4;
    args.dt                   = 1.0f / 60.0f;
    args.baumgarte            = 0.2f;
    args.slop                 = 0.005f;

    phys_stage_constraint_build(&args);

    ASSERT_TRUE(count >= 1);

    /* T1 × T3 → TGS (high-fidelity wins). */
    ASSERT_EQ(PHYS_SOLVER_TGS, constraints[0].solver_mode);

    /* Now test T2 × T3 → XPBD. */
    bodies[0].tier = PHYS_TIER_2_VISIBLE;
    bodies[1].tier = PHYS_TIER_3_WORLD;
    count = 0;

    phys_stage_constraint_build(&args);

    ASSERT_TRUE(count >= 1);
    ASSERT_EQ(PHYS_SOLVER_XPBD, constraints[0].solver_mode);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p075_physics_solver_transition_tests\n");

    RUN_TEST(test_cross_tier_mode_t0_t0);
    RUN_TEST(test_cross_tier_mode_t1_t2);
    RUN_TEST(test_cross_tier_mode_t2_t3);
    RUN_TEST(test_transition_apply_tgs_to_xpbd);
    RUN_TEST(test_transition_apply_xpbd_to_tgs);
    RUN_TEST(test_transition_apply_no_change);
    RUN_TEST(test_constraint_build_sets_solver_mode);

    printf("\n%d/%d tests passed.\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
