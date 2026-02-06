/**
 * @file p045_physics_xpbd_solve_tests.c
 * @brief Unit tests for Stage 11b: XPBD Solve.
 *
 * Tests cover: penetration separation, stability under extreme overlap,
 * static body immobility, velocity derivation, relaxation damping,
 * and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/xpbd_solve.h"

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

/** Create a body at the given position with the specified mass. */
static phys_body_t make_body(float px, float py, float pz, float mass)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position  = (phys_vec3_t){px, py, pz};
    b.linear_vel  = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.flags = 0;
    if (mass > 0.0f) {
        b.inv_mass = 1.0f / mass;
        float inertia = (2.0f / 5.0f) * mass * 0.25f;
        float inv_i = (inertia > 0.0f) ? (1.0f / inertia) : 0.0f;
        b.inv_inertia_diag = (phys_vec3_t){inv_i, inv_i, inv_i};
    } else {
        b.inv_mass = 0.0f;
        b.inv_inertia_diag = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        b.flags = PHYS_BODY_FLAG_STATIC;
    }
    return b;
}

/**
 * @brief Build a contact constraint between two bodies.
 *
 * Wraps phys_constraint_build_contact with a synthetic contact point.
 */
static void build_contact(phys_constraint_t *c,
                          const phys_body_t *bodies,
                          uint32_t idx_a, uint32_t idx_b,
                          phys_vec3_t contact_pt,
                          phys_vec3_t normal,
                          float penetration,
                          float dt)
{
    phys_contact_point_t cp;
    memset(&cp, 0, sizeof(cp));
    cp.point_world = contact_pt;
    cp.normal      = normal;
    cp.penetration = penetration;
    cp.local_a = (phys_vec3_t){
        contact_pt.x - bodies[idx_a].position.x,
        contact_pt.y - bodies[idx_a].position.y,
        contact_pt.z - bodies[idx_a].position.z
    };
    cp.local_b = (phys_vec3_t){
        contact_pt.x - bodies[idx_b].position.x,
        contact_pt.y - bodies[idx_b].position.y,
        contact_pt.z - bodies[idx_b].position.z
    };

    phys_constraint_build_contact(c, &bodies[idx_a], &bodies[idx_b],
                                  &cp, 0.0f, 0.0f,
                                  dt, 0.2f, 0.005f);
    c->body_a = idx_a;
    c->body_b = idx_b;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Two overlapping spheres are separated after solving.
 * Body A at (0,0,0), body B at (0,0.8,0) with radius 0.5 each
 * → penetration = 0.2.  After solving, the gap should increase.
 */
static int test_xpbd_separates_penetrating(void)
{
    const float dt = 1.0f / 60.0f;

    phys_body_t bodies_in[2];
    bodies_in[0] = make_body(0.0f, 0.0f, 0.0f, 1.0f);
    bodies_in[1] = make_body(0.0f, 0.8f, 0.0f, 1.0f);

    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    memset(bodies_out, 0, sizeof(bodies_out));
    memset(velocities, 0, sizeof(velocities));

    /* Contact at midpoint, normal +Y from A to B, penetration 0.2. */
    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact(&constraint, bodies_in, 0, 1,
                  (phys_vec3_t){0.0f, 0.4f, 0.0f},
                  (phys_vec3_t){0.0f, 1.0f, 0.0f},
                  0.2f, dt);

    phys_xpbd_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.constraints      = &constraint;
    args.constraint_count = 1;
    args.bodies_in        = bodies_in;
    args.bodies_out       = bodies_out;
    args.velocities_out   = velocities;
    args.body_count       = 2;
    args.iterations       = 4;
    args.omega            = 1.0f;
    args.dt               = dt;

    phys_stage_xpbd_solve(&args);

    /* After solving, the Y separation should have increased. */
    float initial_gap = bodies_in[1].position.y - bodies_in[0].position.y;
    float final_gap   = bodies_out[1].position.y - bodies_out[0].position.y;
    ASSERT_TRUE(final_gap > initial_gap);

    return 0;
}

/**
 * Test 2: Extreme penetration (bodies at the same position).
 * Verify no explosion / NaN — positions remain finite and reasonable.
 */
static int test_xpbd_stable_large_penetration(void)
{
    const float dt = 1.0f / 60.0f;

    phys_body_t bodies_in[2];
    bodies_in[0] = make_body(0.0f, 0.0f, 0.0f, 1.0f);
    bodies_in[1] = make_body(0.0f, 0.0f, 0.0f, 1.0f); /* same position */

    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    memset(bodies_out, 0, sizeof(bodies_out));
    memset(velocities, 0, sizeof(velocities));

    /* Massive penetration of 5 units along +Y. */
    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact(&constraint, bodies_in, 0, 1,
                  (phys_vec3_t){0.0f, 0.0f, 0.0f},
                  (phys_vec3_t){0.0f, 1.0f, 0.0f},
                  5.0f, dt);

    phys_xpbd_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.constraints      = &constraint;
    args.constraint_count = 1;
    args.bodies_in        = bodies_in;
    args.bodies_out       = bodies_out;
    args.velocities_out   = velocities;
    args.body_count       = 2;
    args.iterations       = 8;
    args.omega            = 0.7f;
    args.dt               = dt;

    phys_stage_xpbd_solve(&args);

    /* Positions must be finite (no NaN / Inf). */
    ASSERT_TRUE(isfinite(bodies_out[0].position.x));
    ASSERT_TRUE(isfinite(bodies_out[0].position.y));
    ASSERT_TRUE(isfinite(bodies_out[0].position.z));
    ASSERT_TRUE(isfinite(bodies_out[1].position.x));
    ASSERT_TRUE(isfinite(bodies_out[1].position.y));
    ASSERT_TRUE(isfinite(bodies_out[1].position.z));

    /* Positions should be reasonable (within, say, 100 units of origin). */
    ASSERT_TRUE(fabsf(bodies_out[0].position.y) < 100.0f);
    ASSERT_TRUE(fabsf(bodies_out[1].position.y) < 100.0f);

    /* Velocities must also be finite. */
    ASSERT_TRUE(isfinite(velocities[0].linear.y));
    ASSERT_TRUE(isfinite(velocities[1].linear.y));

    return 0;
}

/**
 * Test 3: One static body (inv_mass=0) and one dynamic.
 * The static body's position must not change.
 */
static int test_xpbd_static_body_unmoved(void)
{
    const float dt = 1.0f / 60.0f;

    phys_body_t bodies_in[2];
    bodies_in[0] = make_body(0.0f, 0.0f, 0.0f, 0.0f);  /* static floor */
    bodies_in[1] = make_body(0.0f, 0.4f, 0.0f, 1.0f);  /* dynamic ball */

    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    memset(bodies_out, 0, sizeof(bodies_out));
    memset(velocities, 0, sizeof(velocities));

    /* Contact at floor surface, normal +Y, penetration 0.1. */
    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact(&constraint, bodies_in, 0, 1,
                  (phys_vec3_t){0.0f, 0.0f, 0.0f},
                  (phys_vec3_t){0.0f, 1.0f, 0.0f},
                  0.1f, dt);

    phys_xpbd_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.constraints      = &constraint;
    args.constraint_count = 1;
    args.bodies_in        = bodies_in;
    args.bodies_out       = bodies_out;
    args.velocities_out   = velocities;
    args.body_count       = 2;
    args.iterations       = 4;
    args.omega            = 1.0f;
    args.dt               = dt;

    phys_stage_xpbd_solve(&args);

    /* Static body must remain at origin. */
    ASSERT_FLOAT_NEAR(0.0f, bodies_out[0].position.x, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, bodies_out[0].position.y, 1e-6f);
    ASSERT_FLOAT_NEAR(0.0f, bodies_out[0].position.z, 1e-6f);

    /* Dynamic body should have been pushed upward. */
    ASSERT_TRUE(bodies_out[1].position.y > bodies_in[1].position.y);

    return 0;
}

/**
 * Test 4: After solving, velocities_out reflects (pos_out - pos_in) / dt.
 */
static int test_xpbd_velocity_derived(void)
{
    const float dt = 1.0f / 60.0f;

    phys_body_t bodies_in[2];
    bodies_in[0] = make_body(0.0f, 0.0f, 0.0f, 1.0f);
    bodies_in[1] = make_body(0.0f, 0.8f, 0.0f, 1.0f);

    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    memset(bodies_out, 0, sizeof(bodies_out));
    memset(velocities, 0, sizeof(velocities));

    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact(&constraint, bodies_in, 0, 1,
                  (phys_vec3_t){0.0f, 0.4f, 0.0f},
                  (phys_vec3_t){0.0f, 1.0f, 0.0f},
                  0.2f, dt);

    phys_xpbd_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.constraints      = &constraint;
    args.constraint_count = 1;
    args.bodies_in        = bodies_in;
    args.bodies_out       = bodies_out;
    args.velocities_out   = velocities;
    args.body_count       = 2;
    args.iterations       = 4;
    args.omega            = 1.0f;
    args.dt               = dt;

    phys_stage_xpbd_solve(&args);

    /* Derived velocity = (bodies_out - bodies_in) / dt for each body. */
    float inv_dt = 1.0f / dt;
    for (uint32_t i = 0; i < 2; i++) {
        float expected_vx = (bodies_out[i].position.x - bodies_in[i].position.x) * inv_dt;
        float expected_vy = (bodies_out[i].position.y - bodies_in[i].position.y) * inv_dt;
        float expected_vz = (bodies_out[i].position.z - bodies_in[i].position.z) * inv_dt;
        ASSERT_FLOAT_NEAR(expected_vx, velocities[i].linear.x, 1e-3f);
        ASSERT_FLOAT_NEAR(expected_vy, velocities[i].linear.y, 1e-3f);
        ASSERT_FLOAT_NEAR(expected_vz, velocities[i].linear.z, 1e-3f);
    }

    return 0;
}

/**
 * Test 5: With omega=0.5, corrections are damped compared to omega=1.0.
 */
static int test_xpbd_relaxation(void)
{
    const float dt = 1.0f / 60.0f;

    /* Run solver twice with different omega values on identical setup. */
    float gaps[2] = {0.0f, 0.0f};

    for (int run = 0; run < 2; run++) {
        phys_body_t bodies_in[2];
        bodies_in[0] = make_body(0.0f, 0.0f, 0.0f, 1.0f);
        bodies_in[1] = make_body(0.0f, 0.8f, 0.0f, 1.0f);

        phys_body_t bodies_out[2];
        memset(bodies_out, 0, sizeof(bodies_out));

        phys_constraint_t constraint;
        memset(&constraint, 0, sizeof(constraint));
        build_contact(&constraint, bodies_in, 0, 1,
                      (phys_vec3_t){0.0f, 0.4f, 0.0f},
                      (phys_vec3_t){0.0f, 1.0f, 0.0f},
                      0.2f, dt);

        phys_xpbd_solve_args_t args;
        memset(&args, 0, sizeof(args));
        args.constraints      = &constraint;
        args.constraint_count = 1;
        args.bodies_in        = bodies_in;
        args.bodies_out       = bodies_out;
        args.velocities_out   = NULL;  /* not needed for this test */
        args.body_count       = 2;
        args.iterations       = 1;  /* single iteration to see omega effect */
        args.omega            = (run == 0) ? 1.0f : 0.5f;
        args.dt               = dt;

        phys_stage_xpbd_solve(&args);

        gaps[run] = bodies_out[1].position.y - bodies_out[0].position.y;
    }

    /* With omega=1.0, the correction is larger → bigger gap.
     * With omega=0.5, the correction is damped → smaller gap. */
    ASSERT_TRUE(gaps[0] > gaps[1]);

    return 0;
}

/**
 * Test 6: NULL args doesn't crash.
 */
static int test_xpbd_null_safe(void)
{
    phys_stage_xpbd_solve(NULL);

    /* Args with NULL constraints — should not crash. */
    phys_xpbd_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.constraints = NULL;
    phys_stage_xpbd_solve(&args);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p045_physics_xpbd_solve_tests\n");
    RUN_TEST(test_xpbd_separates_penetrating);
    RUN_TEST(test_xpbd_stable_large_penetration);
    RUN_TEST(test_xpbd_static_body_unmoved);
    RUN_TEST(test_xpbd_velocity_derived);
    RUN_TEST(test_xpbd_relaxation);
    RUN_TEST(test_xpbd_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
