/**
 * @file p042_physics_constraint_build_tests.c
 * @brief Unit tests for Stage 9: Constraint Build.
 *
 * Tests cover: single contact, multiple contacts, warmstart loading,
 * stabilization hint application, body/manifold indices, and NULL safety.
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

#define ASSERT_UINT_EQ(exp, act)                                               \
    do {                                                                        \
        unsigned _e = (unsigned)(exp), _a = (unsigned)(act);                    \
        if (_e != _a) {                                                         \
            fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: "                  \
                    "expected %u got %u\n",                                     \
                    __FILE__, __LINE__, _e, _a);                                \
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

/** Build a minimal body for constraint-build testing. */
static phys_body_t make_body(float px, float py, float pz,
                             float inv_mass, uint32_t flags)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position   = (phys_vec3_t){px, py, pz};
    b.inv_mass   = inv_mass;
    b.flags      = flags;
    /* Unit diagonal inverse inertia so effective mass is computable. */
    b.inv_inertia_diag = (phys_vec3_t){1.0f, 1.0f, 1.0f};
    return b;
}

/** Build a manifold with the given number of contact points. */
static phys_manifold_t make_manifold(uint32_t body_a, uint32_t body_b,
                                     uint8_t point_count)
{
    phys_manifold_t m;
    memset(&m, 0, sizeof(m));
    m.body_a      = body_a;
    m.body_b      = body_b;
    m.point_count = point_count;
    m.friction    = 0.5f;
    m.restitution = 0.3f;

    for (uint8_t i = 0; i < point_count; ++i) {
        m.points[i].point_world = (phys_vec3_t){0.0f, 0.5f, 0.0f};
        m.points[i].normal      = (phys_vec3_t){0.0f, 1.0f, 0.0f};
        m.points[i].penetration = 0.01f;
    }
    return m;
}

/** Build a pass-through (identity) stabilization hint. */
static phys_stab_hint_t make_identity_hint(void)
{
    phys_stab_hint_t h;
    h.friction_scale    = 1.0f;
    h.restitution_scale = 1.0f;
    return h;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: One manifold with 1 contact point produces exactly
 * 1 constraint with 3 rows (normal + 2 friction tangent).
 */
static int test_constraint_build_single_contact(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_body(0.0f, 0.0f, 0.0f, 0.0f, PHYS_BODY_FLAG_STATIC);
    bodies[1] = make_body(0.0f, 1.0f, 0.0f, 1.0f, 0);

    phys_manifold_t manifold = make_manifold(0, 1, 1);
    phys_stab_hint_t hint    = make_identity_hint();

    phys_constraint_t constraints[4];
    memset(constraints, 0, sizeof(constraints));
    uint32_t count = 0;

    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds          = &manifold;
    args.hints              = &hint;
    args.manifold_count     = 1;
    args.bodies             = bodies;
    args.constraints_out    = constraints;
    args.constraint_count_out = &count;
    args.max_constraints    = 4;
    args.dt                 = 1.0f / 60.0f;
    args.baumgarte          = 0.2f;
    args.slop               = 0.005f;

    phys_stage_constraint_build(&args);

    ASSERT_UINT_EQ(1, count);
    ASSERT_UINT_EQ(3, constraints[0].row_count);

    return 0;
}

/**
 * Test 2: One manifold with 2 contact points produces 2 constraints.
 */
static int test_constraint_build_multiple_contacts(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_body(0.0f, 0.0f, 0.0f, 0.0f, PHYS_BODY_FLAG_STATIC);
    bodies[1] = make_body(0.0f, 1.0f, 0.0f, 1.0f, 0);

    phys_manifold_t manifold = make_manifold(0, 1, 2);
    /* Give the two contact points distinct positions. */
    manifold.points[0].point_world = (phys_vec3_t){-0.5f, 0.5f, 0.0f};
    manifold.points[1].point_world = (phys_vec3_t){ 0.5f, 0.5f, 0.0f};

    phys_stab_hint_t hint = make_identity_hint();

    phys_constraint_t constraints[8];
    memset(constraints, 0, sizeof(constraints));
    uint32_t count = 0;

    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds          = &manifold;
    args.hints              = &hint;
    args.manifold_count     = 1;
    args.bodies             = bodies;
    args.constraints_out    = constraints;
    args.constraint_count_out = &count;
    args.max_constraints    = 8;
    args.dt                 = 1.0f / 60.0f;
    args.baumgarte          = 0.2f;
    args.slop               = 0.005f;

    phys_stage_constraint_build(&args);

    ASSERT_UINT_EQ(2, count);
    ASSERT_UINT_EQ(3, constraints[0].row_count);
    ASSERT_UINT_EQ(3, constraints[1].row_count);

    return 0;
}

/**
 * Test 3: Warmstart impulses are loaded from the manifold into
 * constraint rows.
 */
static int test_constraint_build_warmstart(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_body(0.0f, 0.0f, 0.0f, 0.0f, PHYS_BODY_FLAG_STATIC);
    bodies[1] = make_body(0.0f, 1.0f, 0.0f, 1.0f, 0);

    phys_manifold_t manifold = make_manifold(0, 1, 1);
    manifold.normal_impulse[0]     = 5.0f;
    manifold.tangent_impulse[0][0] = 1.0f;
    manifold.tangent_impulse[0][1] = 2.0f;

    phys_stab_hint_t hint = make_identity_hint();

    phys_constraint_t constraints[4];
    memset(constraints, 0, sizeof(constraints));
    uint32_t count = 0;

    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds          = &manifold;
    args.hints              = &hint;
    args.manifold_count     = 1;
    args.bodies             = bodies;
    args.constraints_out    = constraints;
    args.constraint_count_out = &count;
    args.max_constraints    = 4;
    args.dt                 = 1.0f / 60.0f;
    args.baumgarte          = 0.2f;
    args.slop               = 0.005f;

    phys_stage_constraint_build(&args);

    ASSERT_UINT_EQ(1, count);
    ASSERT_FLOAT_NEAR(5.0f, constraints[0].rows[0].lambda, 1e-5f);
    ASSERT_FLOAT_NEAR(1.0f, constraints[0].rows[1].lambda, 1e-5f);
    ASSERT_FLOAT_NEAR(2.0f, constraints[0].rows[2].lambda, 1e-5f);

    return 0;
}

/**
 * Test 4: Stabilization hint with friction_scale=3.0 multiplies
 * the manifold friction.  We verify that the friction tangent rows'
 * lambda_max reflects the scaled friction (friction * 3 * BIG).
 */
static int test_constraint_build_stab_hints(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_body(0.0f, 0.0f, 0.0f, 0.0f, PHYS_BODY_FLAG_STATIC);
    bodies[1] = make_body(0.0f, 1.0f, 0.0f, 1.0f, 0);

    phys_manifold_t manifold = make_manifold(0, 1, 1);
    manifold.friction    = 0.5f;
    manifold.restitution = 0.4f;

    /* Resting hint: boost friction by 3x, suppress restitution. */
    phys_stab_hint_t hint;
    hint.friction_scale    = 3.0f;
    hint.restitution_scale = 0.0f;

    phys_constraint_t constraints[4];
    memset(constraints, 0, sizeof(constraints));
    uint32_t count = 0;

    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds          = &manifold;
    args.hints              = &hint;
    args.manifold_count     = 1;
    args.bodies             = bodies;
    args.constraints_out    = constraints;
    args.constraint_count_out = &count;
    args.max_constraints    = 4;
    args.dt                 = 1.0f / 60.0f;
    args.baumgarte          = 0.2f;
    args.slop               = 0.005f;

    phys_stage_constraint_build(&args);

    ASSERT_UINT_EQ(1, count);

    /*
     * The effective friction passed to phys_constraint_build_contact
     * is 0.5 * 3.0 = 1.5.  The friction tangent rows use
     * lambda_max = friction * 1e10, lambda_min = -friction * 1e10.
     */
    float expected_friction = 0.5f * 3.0f;  /* 1.5 */
    float big = 1e10f;
    ASSERT_FLOAT_NEAR( expected_friction * big,
                       constraints[0].rows[1].lambda_max, 1e4f);
    ASSERT_FLOAT_NEAR(-expected_friction * big,
                       constraints[0].rows[1].lambda_min, 1e4f);

    /* Also verify for row 2 (second tangent). */
    ASSERT_FLOAT_NEAR( expected_friction * big,
                       constraints[0].rows[2].lambda_max, 1e4f);
    ASSERT_FLOAT_NEAR(-expected_friction * big,
                       constraints[0].rows[2].lambda_min, 1e4f);

    return 0;
}

/**
 * Test 5: Body indices, manifold_idx, and point_idx are set correctly.
 */
static int test_constraint_build_body_indices(void)
{
    phys_body_t bodies[4];
    bodies[0] = make_body(0.0f, 0.0f, 0.0f, 0.0f, PHYS_BODY_FLAG_STATIC);
    bodies[1] = make_body(0.0f, 1.0f, 0.0f, 1.0f, 0);
    bodies[2] = make_body(5.0f, 0.0f, 0.0f, 0.0f, PHYS_BODY_FLAG_STATIC);
    bodies[3] = make_body(5.0f, 1.0f, 0.0f, 1.0f, 0);

    /* Manifold 0: bodies 0-1, 2 points. */
    phys_manifold_t manifolds[2];
    manifolds[0] = make_manifold(0, 1, 2);
    manifolds[0].points[0].point_world = (phys_vec3_t){-0.5f, 0.5f, 0.0f};
    manifolds[0].points[1].point_world = (phys_vec3_t){ 0.5f, 0.5f, 0.0f};

    /* Manifold 1: bodies 2-3, 1 point. */
    manifolds[1] = make_manifold(2, 3, 1);
    manifolds[1].points[0].point_world = (phys_vec3_t){5.0f, 0.5f, 0.0f};

    phys_stab_hint_t hints[2];
    hints[0] = make_identity_hint();
    hints[1] = make_identity_hint();

    phys_constraint_t constraints[8];
    memset(constraints, 0, sizeof(constraints));
    uint32_t count = 0;

    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    args.manifolds          = manifolds;
    args.hints              = hints;
    args.manifold_count     = 2;
    args.bodies             = bodies;
    args.constraints_out    = constraints;
    args.constraint_count_out = &count;
    args.max_constraints    = 8;
    args.dt                 = 1.0f / 60.0f;
    args.baumgarte          = 0.2f;
    args.slop               = 0.005f;

    phys_stage_constraint_build(&args);

    ASSERT_UINT_EQ(3, count);

    /* Constraint 0: manifold 0, point 0. */
    ASSERT_UINT_EQ(0, constraints[0].body_a);
    ASSERT_UINT_EQ(1, constraints[0].body_b);
    ASSERT_UINT_EQ(0, constraints[0].manifold_idx);
    ASSERT_UINT_EQ(0, constraints[0].point_idx);

    /* Constraint 1: manifold 0, point 1. */
    ASSERT_UINT_EQ(0, constraints[1].body_a);
    ASSERT_UINT_EQ(1, constraints[1].body_b);
    ASSERT_UINT_EQ(0, constraints[1].manifold_idx);
    ASSERT_UINT_EQ(1, constraints[1].point_idx);

    /* Constraint 2: manifold 1, point 0. */
    ASSERT_UINT_EQ(2, constraints[2].body_a);
    ASSERT_UINT_EQ(3, constraints[2].body_b);
    ASSERT_UINT_EQ(1, constraints[2].manifold_idx);
    ASSERT_UINT_EQ(0, constraints[2].point_idx);

    return 0;
}

/**
 * Test 6: NULL args and NULL internals do not crash.
 */
static int test_constraint_build_null_safe(void)
{
    /* NULL args — should be a safe no-op. */
    phys_stage_constraint_build(NULL);

    /* Args with NULL internals — should not crash. */
    phys_constraint_build_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_constraint_build(&args);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p042_physics_constraint_build_tests\n");

    RUN_TEST(test_constraint_build_single_contact);
    RUN_TEST(test_constraint_build_multiple_contacts);
    RUN_TEST(test_constraint_build_warmstart);
    RUN_TEST(test_constraint_build_stab_hints);
    RUN_TEST(test_constraint_build_body_indices);
    RUN_TEST(test_constraint_build_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
