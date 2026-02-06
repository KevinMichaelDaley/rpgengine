/**
 * @file p044_physics_tgs_solve_tests.c
 * @brief Unit tests for Stage 11a: TGS Solve.
 *
 * Tests cover: simple collision, static floor, momentum conservation,
 * island independence, sleeping island skip, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/constraint.h"
#include "ferrum/physics/island.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/tgs_solve.h"

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

/** Create a dynamic body at position with given velocity and mass. */
static phys_body_t make_body(float px, float py, float pz,
                             float vx, float vy, float vz,
                             float mass)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position   = (phys_vec3_t){px, py, pz};
    b.linear_vel = (phys_vec3_t){vx, vy, vz};
    b.angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.flags = 0;
    if (mass > 0.0f) {
        b.inv_mass = 1.0f / mass;
        /* Sphere inertia: I = (2/5)*m*r^2, using r=0.5 */
        float inertia = (2.0f / 5.0f) * mass * 0.25f;
        float inv_inertia = (inertia > 0.0f) ? (1.0f / inertia) : 0.0f;
        b.inv_inertia_diag = (phys_vec3_t){inv_inertia, inv_inertia, inv_inertia};
    } else {
        /* Static body. */
        b.inv_mass = 0.0f;
        b.inv_inertia_diag = (phys_vec3_t){0.0f, 0.0f, 0.0f};
        b.flags = PHYS_BODY_FLAG_STATIC;
    }
    return b;
}

/**
 * @brief Build a normal-only contact constraint between two bodies.
 *
 * Uses phys_constraint_build_contact to produce a proper constraint
 * with Jacobians and effective mass.
 */
static void build_contact_constraint(phys_constraint_t *c,
                                     const phys_body_t *bodies,
                                     uint32_t idx_a, uint32_t idx_b,
                                     phys_vec3_t contact_point,
                                     phys_vec3_t normal,
                                     float penetration,
                                     float friction,
                                     float restitution,
                                     float dt)
{
    phys_contact_point_t cp;
    memset(&cp, 0, sizeof(cp));
    cp.point_world = contact_point;
    cp.normal      = normal;
    cp.penetration = penetration;
    /* Local-space offsets from body center. */
    cp.local_a = (phys_vec3_t){
        contact_point.x - bodies[idx_a].position.x,
        contact_point.y - bodies[idx_a].position.y,
        contact_point.z - bodies[idx_a].position.z
    };
    cp.local_b = (phys_vec3_t){
        contact_point.x - bodies[idx_b].position.x,
        contact_point.y - bodies[idx_b].position.y,
        contact_point.z - bodies[idx_b].position.z
    };

    phys_constraint_build_contact(c, &bodies[idx_a], &bodies[idx_b],
                                  &cp, friction, restitution,
                                  dt, 0.2f, 0.005f);
    c->body_a = idx_a;
    c->body_b = idx_b;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Two equal-mass spheres approaching each other along Y.
 * After solving, the relative velocity at the contact should be
 * near zero (no bounce, restitution=0).
 */
static int test_tgs_simple_collision(void)
{
    /* Body 0 moving +Y, body 1 moving -Y. */
    phys_body_t bodies[2];
    bodies[0] = make_body(0, 0, 0,   0,  1, 0,  1.0f);
    bodies[1] = make_body(0, 1, 0,   0, -1, 0,  1.0f);

    /* Contact at midpoint, normal pointing from A to B (+Y). */
    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact_constraint(&constraint, bodies, 0, 1,
                             (phys_vec3_t){0, 0.5f, 0},
                             (phys_vec3_t){0, 1, 0},
                             0.01f, 0.0f, 0.0f, 1.0f / 60.0f);

    /* Island: both bodies, one constraint. */
    uint32_t body_indices[2] = {0, 1};
    uint32_t constraint_indices[1] = {0};
    phys_island_t island;
    memset(&island, 0, sizeof(island));
    island.body_indices       = body_indices;
    island.body_count         = 2;
    island.constraint_indices = constraint_indices;
    island.constraint_count   = 1;
    island.sleeping           = false;

    phys_island_list_t island_list;
    memset(&island_list, 0, sizeof(island_list));
    island_list.islands  = &island;
    island_list.count    = 1;
    island_list.capacity = 1;

    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));

    phys_tgs_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.islands      = &island_list;
    args.constraints  = &constraint;
    args.bodies       = bodies;
    args.velocities   = velocities;
    args.body_count   = 2;
    args.iterations   = 20;

    phys_stage_tgs_solve(&args);

    /* Relative approach velocity along normal should be ~0 (no penetration). */
    float rel_vy = velocities[0].linear.y - velocities[1].linear.y;
    ASSERT_TRUE(rel_vy <= 0.05f);

    return 0;
}

/**
 * Test 2: Ball falling toward a static floor.
 * After solving, the ball's downward velocity should be reduced.
 */
static int test_tgs_static_floor(void)
{
    phys_body_t bodies[2];
    /* Body 0: static floor. */
    bodies[0] = make_body(0, 0, 0,  0, 0, 0,  0.0f);
    /* Body 1: ball falling down. */
    bodies[1] = make_body(0, 0.5f, 0,  0, -5.0f, 0,  1.0f);

    /* Contact at floor surface, normal pointing up (+Y, from A to B). */
    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact_constraint(&constraint, bodies, 0, 1,
                             (phys_vec3_t){0, 0.0f, 0},
                             (phys_vec3_t){0, 1, 0},
                             0.01f, 0.3f, 0.0f, 1.0f / 60.0f);

    uint32_t body_indices[2] = {0, 1};
    uint32_t constraint_indices[1] = {0};
    phys_island_t island;
    memset(&island, 0, sizeof(island));
    island.body_indices       = body_indices;
    island.body_count         = 2;
    island.constraint_indices = constraint_indices;
    island.constraint_count   = 1;
    island.sleeping           = false;

    phys_island_list_t island_list;
    memset(&island_list, 0, sizeof(island_list));
    island_list.islands  = &island;
    island_list.count    = 1;
    island_list.capacity = 1;

    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));

    phys_tgs_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.islands      = &island_list;
    args.constraints  = &constraint;
    args.bodies       = bodies;
    args.velocities   = velocities;
    args.body_count   = 2;
    args.iterations   = 20;

    phys_stage_tgs_solve(&args);

    /* Ball's downward velocity should be reduced (normal impulse applied). */
    ASSERT_TRUE(velocities[1].linear.y > -5.0f);
    /* Static floor should remain at zero velocity. */
    ASSERT_FLOAT_NEAR(0.0f, velocities[0].linear.y, 1e-6f);

    return 0;
}

/**
 * Test 3: Two equal-mass bodies colliding head-on.
 * Total momentum should be conserved.
 */
static int test_tgs_momentum_conservation(void)
{
    float mass = 2.0f;
    phys_body_t bodies[2];
    bodies[0] = make_body(0, 0, 0,   0,  3.0f, 0,  mass);
    bodies[1] = make_body(0, 1, 0,   0, -3.0f, 0,  mass);

    /* Initial total momentum along Y: mass*3 + mass*(-3) = 0. */
    float initial_momentum = mass * bodies[0].linear_vel.y
                           + mass * bodies[1].linear_vel.y;

    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact_constraint(&constraint, bodies, 0, 1,
                             (phys_vec3_t){0, 0.5f, 0},
                             (phys_vec3_t){0, 1, 0},
                             0.01f, 0.0f, 0.0f, 1.0f / 60.0f);

    uint32_t body_indices[2] = {0, 1};
    uint32_t constraint_indices[1] = {0};
    phys_island_t island;
    memset(&island, 0, sizeof(island));
    island.body_indices       = body_indices;
    island.body_count         = 2;
    island.constraint_indices = constraint_indices;
    island.constraint_count   = 1;
    island.sleeping           = false;

    phys_island_list_t island_list;
    memset(&island_list, 0, sizeof(island_list));
    island_list.islands  = &island;
    island_list.count    = 1;
    island_list.capacity = 1;

    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));

    phys_tgs_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.islands      = &island_list;
    args.constraints  = &constraint;
    args.bodies       = bodies;
    args.velocities   = velocities;
    args.body_count   = 2;
    args.iterations   = 20;

    phys_stage_tgs_solve(&args);

    /* Total momentum should be conserved. */
    float final_momentum = mass * velocities[0].linear.y
                         + mass * velocities[1].linear.y;
    ASSERT_FLOAT_NEAR(initial_momentum, final_momentum, 0.01f);

    return 0;
}

/**
 * Test 4: Two separate islands with different constraints.
 * Solving one island must not affect the other.
 */
static int test_tgs_island_independence(void)
{
    /* 4 bodies: island A = {0,1}, island B = {2,3}.
     * Use different masses so results are distinguishable. */
    phys_body_t bodies[4];
    bodies[0] = make_body(0, 0, 0,   0,  1, 0,  1.0f);
    bodies[1] = make_body(0, 1, 0,   0, -1, 0,  1.0f);
    bodies[2] = make_body(5, 0, 0,   0,  2, 0,  3.0f);
    bodies[3] = make_body(5, 1, 0,   0, -2, 0,  1.0f);

    phys_constraint_t constraints[2];
    memset(constraints, 0, sizeof(constraints));

    /* Constraint 0: bodies 0-1. */
    build_contact_constraint(&constraints[0], bodies, 0, 1,
                             (phys_vec3_t){0, 0.5f, 0},
                             (phys_vec3_t){0, 1, 0},
                             0.01f, 0.0f, 0.0f, 1.0f / 60.0f);
    /* Constraint 1: bodies 2-3. */
    build_contact_constraint(&constraints[1], bodies, 2, 3,
                             (phys_vec3_t){5, 0.5f, 0},
                             (phys_vec3_t){0, 1, 0},
                             0.01f, 0.0f, 0.0f, 1.0f / 60.0f);

    /* Island A: bodies {0,1}, constraint {0}. */
    uint32_t body_idx_a[2] = {0, 1};
    uint32_t con_idx_a[1]  = {0};
    /* Island B: bodies {2,3}, constraint {1}. */
    uint32_t body_idx_b[2] = {2, 3};
    uint32_t con_idx_b[1]  = {1};

    phys_island_t islands[2];
    memset(islands, 0, sizeof(islands));
    islands[0].body_indices       = body_idx_a;
    islands[0].body_count         = 2;
    islands[0].constraint_indices = con_idx_a;
    islands[0].constraint_count   = 1;
    islands[0].sleeping           = false;

    islands[1].body_indices       = body_idx_b;
    islands[1].body_count         = 2;
    islands[1].constraint_indices = con_idx_b;
    islands[1].constraint_count   = 1;
    islands[1].sleeping           = false;

    phys_island_list_t island_list;
    memset(&island_list, 0, sizeof(island_list));
    island_list.islands  = islands;
    island_list.count    = 2;
    island_list.capacity = 2;

    phys_velocity_t velocities[4];
    memset(velocities, 0, sizeof(velocities));

    phys_tgs_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.islands      = &island_list;
    args.constraints  = constraints;
    args.bodies       = bodies;
    args.velocities   = velocities;
    args.body_count   = 4;
    args.iterations   = 20;

    phys_stage_tgs_solve(&args);

    /* Island A bodies (0,1) should be solved independently of B (2,3).
     * Verify bodies in each island have been modified from their initial
     * velocities (the solver did work). */
    float rel_a = velocities[0].linear.y - velocities[1].linear.y;
    float rel_b = velocities[2].linear.y - velocities[3].linear.y;

    /* Both islands should have their approach velocity reduced. */
    ASSERT_TRUE(rel_a < 2.0f);   /* Was 2 initially. */
    ASSERT_TRUE(rel_b < 4.0f);   /* Was 4 initially. */

    /* Cross-island: island B had higher velocity, so its results differ. */
    /* Bodies 0-1 results should differ from bodies 2-3 results. */
    ASSERT_TRUE(fabsf(velocities[0].linear.y - velocities[2].linear.y) > 0.01f ||
                fabsf(velocities[1].linear.y - velocities[3].linear.y) > 0.01f);

    return 0;
}

/**
 * Test 5: Island marked sleeping → velocities unchanged.
 */
static int test_tgs_sleeping_island_skipped(void)
{
    phys_body_t bodies[2];
    bodies[0] = make_body(0, 0, 0,   0,  1, 0,  1.0f);
    bodies[1] = make_body(0, 1, 0,   0, -1, 0,  1.0f);

    phys_constraint_t constraint;
    memset(&constraint, 0, sizeof(constraint));
    build_contact_constraint(&constraint, bodies, 0, 1,
                             (phys_vec3_t){0, 0.5f, 0},
                             (phys_vec3_t){0, 1, 0},
                             0.01f, 0.0f, 0.0f, 1.0f / 60.0f);

    uint32_t body_indices[2] = {0, 1};
    uint32_t constraint_indices[1] = {0};
    phys_island_t island;
    memset(&island, 0, sizeof(island));
    island.body_indices       = body_indices;
    island.body_count         = 2;
    island.constraint_indices = constraint_indices;
    island.constraint_count   = 1;
    island.sleeping           = true;  /* Mark as sleeping. */

    phys_island_list_t island_list;
    memset(&island_list, 0, sizeof(island_list));
    island_list.islands  = &island;
    island_list.count    = 1;
    island_list.capacity = 1;

    phys_velocity_t velocities[2];
    memset(velocities, 0, sizeof(velocities));

    phys_tgs_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.islands      = &island_list;
    args.constraints  = &constraint;
    args.bodies       = bodies;
    args.velocities   = velocities;
    args.body_count   = 2;
    args.iterations   = 20;

    phys_stage_tgs_solve(&args);

    /* Velocities should be initialized from bodies but NOT solved
     * (constraints skipped). So they should match the original body vels. */
    ASSERT_FLOAT_NEAR( 1.0f, velocities[0].linear.y, 1e-6f);
    ASSERT_FLOAT_NEAR(-1.0f, velocities[1].linear.y, 1e-6f);
    /* Lambda should remain at zero (untouched). */
    ASSERT_FLOAT_NEAR(0.0f, constraint.rows[0].lambda, 1e-6f);

    return 0;
}

/**
 * Test 6: NULL args doesn't crash.
 */
static int test_tgs_null_safe(void)
{
    /* NULL args — should not crash. */
    phys_stage_tgs_solve(NULL);

    /* Args with NULL islands — should not crash. */
    phys_tgs_solve_args_t args;
    memset(&args, 0, sizeof(args));
    args.islands = NULL;
    phys_stage_tgs_solve(&args);

    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p044_physics_tgs_solve_tests\n");
    RUN_TEST(test_tgs_simple_collision);
    RUN_TEST(test_tgs_static_floor);
    RUN_TEST(test_tgs_momentum_conservation);
    RUN_TEST(test_tgs_island_independence);
    RUN_TEST(test_tgs_sleeping_island_skipped);
    RUN_TEST(test_tgs_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
