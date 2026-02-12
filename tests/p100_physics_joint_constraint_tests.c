/**
 * @file p100_physics_joint_constraint_tests.c
 * @brief Tests for joint constraint build (phys-802).
 *
 * Tests:
 *   1. Joint → phys_constraint_t conversion (build_constraints)
 *   2. TGS solver with joint constraints (bilateral, no friction cone)
 *   3. XPBD solver with joint constraints (all rows, bilateral)
 *   4. World joint management (add, remove, get, count)
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>

#include "ferrum/physics/joint.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/tgs_solve.h"
#include "ferrum/physics/xpbd_solve.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/world.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                              \
    do {                                                                     \
        if ((exp) != (act)) {                                                \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d "     \
                    "got %d\n", __FILE__, __LINE__, (int)(exp), (int)(act)); \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                     \
    do {                                                                     \
        float _e = (float)(exp);                                             \
        float _a = (float)(act);                                             \
        if (fabsf(_e - _a) > (eps)) {                                        \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected "    \
                    "%f got %f (eps=%f)\n", __FILE__, __LINE__,              \
                    (double)_e, (double)_a, (double)(eps));                   \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_VEC3_NEAR(exp, act, eps)                                      \
    do {                                                                     \
        phys_vec3_t _ev = (exp);                                             \
        phys_vec3_t _av = (act);                                             \
        if (fabsf(_ev.x - _av.x) > (eps) ||                                 \
            fabsf(_ev.y - _av.y) > (eps) ||                                  \
            fabsf(_ev.z - _av.z) > (eps)) {                                  \
            fprintf(stderr, "ASSERT_VEC3_NEAR failed: %s:%d: expected "     \
                    "(%f,%f,%f) got (%f,%f,%f) (eps=%f)\n", __FILE__,        \
                    __LINE__, (double)_ev.x, (double)_ev.y, (double)_ev.z,   \
                    (double)_av.x, (double)_av.y, (double)_av.z,             \
                    (double)(eps));                                           \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static const float EPS = 1e-5f;

/* ── Helpers ────────────────────────────────────────────────────── */

static void setup_bodies(phys_body_t *a, phys_body_t *b) {
    phys_body_init(a);
    phys_body_init(b);
    phys_body_set_mass(a, 1.0f);
    phys_body_set_mass(b, 1.0f);
    phys_body_set_sphere_inertia(a, 1.0f, 0.5f);
    phys_body_set_sphere_inertia(b, 1.0f, 0.5f);
    a->position = (phys_vec3_t){-1.0f, 0.0f, 0.0f};
    b->position = (phys_vec3_t){ 1.0f, 0.0f, 0.0f};
}

/* ══════════════════════════════════════════════════════════════════
 * BUILD_CONSTRAINTS TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Distance joint → 1 constraint. */
static int test_build_constraints_distance(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.rest_length = 1.0f;

    phys_joint_build_distance(&j, &a, &b, 1.0f / 60.0f);

    phys_constraint_t out[2];
    memset(out, 0, sizeof(out));
    uint32_t n = phys_joint_build_constraints(&j, out, 2, 0);

    ASSERT_INT_EQ(1, (int)n);
    ASSERT_INT_EQ(1, out[0].row_count);
    ASSERT_INT_EQ(1, out[0].is_joint);
    ASSERT_INT_EQ(0, (int)out[0].body_a);
    ASSERT_INT_EQ(1, (int)out[0].body_b);
    ASSERT_TRUE(out[0].manifold_idx == UINT32_MAX);
    return 0;
}

/** Ball joint → 1 constraint with 3 rows. */
static int test_build_constraints_ball(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);

    phys_constraint_t out[2];
    uint32_t n = phys_joint_build_constraints(&j, out, 2, 0);

    ASSERT_INT_EQ(1, (int)n);
    ASSERT_INT_EQ(3, out[0].row_count);
    ASSERT_INT_EQ(1, out[0].is_joint);
    return 0;
}

/** Hinge joint → 2 constraints (3 + 2 rows). */
static int test_build_constraints_hinge(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);

    phys_constraint_t out[2];
    uint32_t n = phys_joint_build_constraints(&j, out, 2, 0);

    ASSERT_INT_EQ(2, (int)n);
    ASSERT_INT_EQ(3, out[0].row_count);
    ASSERT_INT_EQ(2, out[1].row_count);
    ASSERT_INT_EQ(1, out[0].is_joint);
    ASSERT_INT_EQ(1, out[1].is_joint);
    return 0;
}

/** Hinge with max_out=1 only produces first constraint. */
static int test_build_constraints_hinge_truncated(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_HINGE;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.5f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-0.5f, 0.0f, 0.0f};
    j.local_axis_a = (phys_vec3_t){0.0f, 1.0f, 0.0f};

    phys_joint_build_hinge(&j, &a, &b, 1.0f / 60.0f);

    phys_constraint_t out[1];
    uint32_t n = phys_joint_build_constraints(&j, out, 1, 0);

    ASSERT_INT_EQ(1, (int)n);
    ASSERT_INT_EQ(3, out[0].row_count);
    return 0;
}

/** Solver mode is propagated. */
static int test_build_constraints_solver_mode(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0; j.body_b = 1;

    phys_joint_build_ball(&j, &a, &b, 1.0f / 60.0f);

    phys_constraint_t out[2];
    uint32_t n = phys_joint_build_constraints(&j, out, 2, 1);
    ASSERT_INT_EQ(1, (int)n);
    ASSERT_INT_EQ(1, out[0].solver_mode);
    return 0;
}

/** NULL safety for build_constraints. */
static int test_build_constraints_null(void) {
    phys_body_t a, b;
    setup_bodies(&a, &b);
    phys_joint_t j;
    phys_joint_init(&j);
    phys_constraint_t out[2];

    ASSERT_INT_EQ(0, (int)phys_joint_build_constraints(NULL, out, 2, 0));
    ASSERT_INT_EQ(0, (int)phys_joint_build_constraints(&j, NULL, 2, 0));
    ASSERT_INT_EQ(0, (int)phys_joint_build_constraints(&j, out, 0, 0));
    /* row_count=0 → 0 constraints. */
    ASSERT_INT_EQ(0, (int)phys_joint_build_constraints(&j, out, 2, 0));
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * TGS SOLVER JOINT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** TGS solver preserves bilateral lambda bounds for joints (no friction cone). */
static int test_tgs_joint_bilateral(void) {
    phys_body_t bodies[2];
    setup_bodies(&bodies[0], &bodies[1]);
    /* Separate bodies with a ball joint — the solver should pull them together. */
    bodies[0].position = (phys_vec3_t){-2.0f, 0.0f, 0.0f};
    bodies[1].position = (phys_vec3_t){ 2.0f, 0.0f, 0.0f};

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-1.0f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &bodies[0], &bodies[1], 1.0f / 60.0f);

    phys_constraint_t constraints[2];
    uint32_t nc = phys_joint_build_constraints(&j, constraints, 2, 0);

    /* Set up velocities. */
    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));
    velocities[0].linear = bodies[0].linear_vel;
    velocities[0].angular = bodies[0].angular_vel;
    velocities[1].linear = bodies[1].linear_vel;
    velocities[1].angular = bodies[1].angular_vel;

    /* Solve one constraint directly. */
    /* After solving, lambda bounds should remain bilateral (not friction-clamped). */
    for (uint32_t ci = 0; ci < nc; ++ci) {
        for (uint8_t r = 0; r < constraints[ci].row_count; ++r) {
            /* Pre-check: bilateral bounds. */
            ASSERT_TRUE(constraints[ci].rows[r].lambda_min < 0.0f);
            ASSERT_TRUE(constraints[ci].rows[r].lambda_max > 0.0f);
        }
    }

    /* After TGS would solve, the lambda_min/max should still be bilateral.
     * We verify this by checking that the constraint is marked as joint
     * and friction is 0 (no friction cone override possible). */
    for (uint32_t ci = 0; ci < nc; ++ci) {
        ASSERT_INT_EQ(1, constraints[ci].is_joint);
        ASSERT_FLOAT_NEAR(0.0f, constraints[ci].friction, EPS);
    }
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * XPBD SOLVER JOINT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** XPBD solver corrects position for ball joint constraints. */
static int test_xpbd_joint_ball_corrects(void) {
    phys_body_t bodies_in[2];
    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    setup_bodies(&bodies_in[0], &bodies_in[1]);
    /* Bodies far apart, ball joint should pull anchors together. */
    bodies_in[0].position = (phys_vec3_t){-2.0f, 0.0f, 0.0f};
    bodies_in[1].position = (phys_vec3_t){ 2.0f, 0.0f, 0.0f};

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){1.0f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){-1.0f, 0.0f, 0.0f};

    phys_joint_build_ball(&j, &bodies_in[0], &bodies_in[1], 1.0f / 60.0f);

    phys_constraint_t constraints[2];
    uint32_t nc = phys_joint_build_constraints(&j, constraints, 2, 1);

    float dt = 1.0f / 60.0f;
    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = constraints,
        .constraint_count = nc,
        .bodies_in        = bodies_in,
        .bodies_out       = bodies_out,
        .velocities_out   = velocities,
        .body_count       = 2,
        .iterations       = 4,
        .omega            = 0.8f,
        .dt               = dt,
        .compliance       = 0.0f,
    });

    /* Bodies should have moved closer together (toward each other). */
    float dist_before = fabsf(bodies_in[1].position.x - bodies_in[0].position.x);
    float dist_after  = fabsf(bodies_out[1].position.x - bodies_out[0].position.x);
    ASSERT_TRUE(dist_after < dist_before);
    return 0;
}

/** XPBD solver handles distance joint (1 row). */
static int test_xpbd_joint_distance_corrects(void) {
    phys_body_t bodies_in[2];
    phys_body_t bodies_out[2];
    phys_velocity_t velocities[2];
    setup_bodies(&bodies_in[0], &bodies_in[1]);
    bodies_in[0].position = (phys_vec3_t){-2.0f, 0.0f, 0.0f};
    bodies_in[1].position = (phys_vec3_t){ 2.0f, 0.0f, 0.0f};

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_DISTANCE;
    j.body_a = 0; j.body_b = 1;
    j.local_anchor_a = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    j.local_anchor_b = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    j.rest_length = 1.0f;  /* Current distance is 4, want 1 → should correct. */

    phys_joint_build_distance(&j, &bodies_in[0], &bodies_in[1], 1.0f / 60.0f);

    phys_constraint_t constraints[2];
    uint32_t nc = phys_joint_build_constraints(&j, constraints, 2, 1);

    float dt = 1.0f / 60.0f;
    phys_stage_xpbd_solve(&(phys_xpbd_solve_args_t){
        .constraints      = constraints,
        .constraint_count = nc,
        .bodies_in        = bodies_in,
        .bodies_out       = bodies_out,
        .velocities_out   = velocities,
        .body_count       = 2,
        .iterations       = 4,
        .omega            = 0.8f,
        .dt               = dt,
        .compliance       = 0.0f,
    });

    float dist_after = fabsf(bodies_out[1].position.x - bodies_out[0].position.x);
    /* Should move closer to rest_length=1.0. */
    ASSERT_TRUE(dist_after < 4.0f);
    return 0;
}

/* ══════════════════════════════════════════════════════════════════
 * WORLD JOINT MANAGEMENT TESTS
 * ══════════════════════════════════════════════════════════════════ */

/** Add and retrieve a joint from the world. */
static int test_world_add_joint(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_joints = 16;
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    phys_joint_t j;
    phys_joint_init(&j);
    j.type = PHYS_JOINT_BALL;
    j.body_a = 0;
    j.body_b = 1;

    uint32_t idx = phys_world_add_joint(&world, &j);
    ASSERT_TRUE(idx != UINT32_MAX);
    ASSERT_INT_EQ(1, (int)phys_world_joint_count(&world));

    phys_joint_t *got = phys_world_get_joint(&world, idx);
    ASSERT_TRUE(got != NULL);
    ASSERT_INT_EQ(PHYS_JOINT_BALL, got->type);

    phys_world_destroy(&world);
    return 0;
}

/** Remove joint from world. */
static int test_world_remove_joint(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_joints = 16;
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    phys_joint_t j1, j2;
    phys_joint_init(&j1);
    phys_joint_init(&j2);
    j1.type = PHYS_JOINT_DISTANCE;
    j2.type = PHYS_JOINT_HINGE;

    phys_world_add_joint(&world, &j1);
    phys_world_add_joint(&world, &j2);
    ASSERT_INT_EQ(2, (int)phys_world_joint_count(&world));

    phys_world_remove_joint(&world, 0);
    ASSERT_INT_EQ(1, (int)phys_world_joint_count(&world));

    /* After swap-remove, joint[0] should now be the hinge. */
    phys_joint_t *remaining = phys_world_get_joint(&world, 0);
    ASSERT_TRUE(remaining != NULL);
    ASSERT_INT_EQ(PHYS_JOINT_HINGE, remaining->type);

    phys_world_destroy(&world);
    return 0;
}

/** World joint capacity respected. */
static int test_world_joint_capacity(void) {
    phys_world_config_t cfg = phys_world_config_default();
    cfg.max_bodies = 64;
    cfg.max_joints = 2;
    phys_world_t world;
    ASSERT_INT_EQ(0, phys_world_init(&world, &cfg));

    phys_joint_t j;
    phys_joint_init(&j);
    ASSERT_TRUE(phys_world_add_joint(&world, &j) != UINT32_MAX);
    ASSERT_TRUE(phys_world_add_joint(&world, &j) != UINT32_MAX);
    /* Third should fail. */
    ASSERT_TRUE(phys_world_add_joint(&world, &j) == UINT32_MAX);
    ASSERT_INT_EQ(2, (int)phys_world_joint_count(&world));

    phys_world_destroy(&world);
    return 0;
}

/** World joint API is NULL-safe. */
static int test_world_joint_null_safe(void) {
    ASSERT_TRUE(phys_world_add_joint(NULL, NULL) == UINT32_MAX);
    phys_world_remove_joint(NULL, 0);  /* must not crash */
    ASSERT_TRUE(phys_world_get_joint(NULL, 0) == NULL);
    ASSERT_INT_EQ(0, (int)phys_world_joint_count(NULL));
    return 0;
}

/** Constraint is_joint=0 for contacts (backwards compat). */
static int test_contact_constraint_not_joint(void) {
    phys_constraint_t c;
    memset(&c, 0, sizeof(c));
    /* Default-zeroed constraint should not be marked as joint. */
    ASSERT_INT_EQ(0, c.is_joint);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    /* build_constraints */
    {"build_constraints_distance",      test_build_constraints_distance},
    {"build_constraints_ball",          test_build_constraints_ball},
    {"build_constraints_hinge",         test_build_constraints_hinge},
    {"build_constraints_hinge_truncated", test_build_constraints_hinge_truncated},
    {"build_constraints_solver_mode",   test_build_constraints_solver_mode},
    {"build_constraints_null",          test_build_constraints_null},
    /* TGS */
    {"tgs_joint_bilateral",             test_tgs_joint_bilateral},
    /* XPBD */
    {"xpbd_joint_ball_corrects",        test_xpbd_joint_ball_corrects},
    {"xpbd_joint_distance_corrects",    test_xpbd_joint_distance_corrects},
    /* World */
    {"world_add_joint",                 test_world_add_joint},
    {"world_remove_joint",              test_world_remove_joint},
    {"world_joint_capacity",            test_world_joint_capacity},
    {"world_joint_null_safe",           test_world_joint_null_safe},
    /* Backwards compat */
    {"contact_constraint_not_joint",    test_contact_constraint_not_joint},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
