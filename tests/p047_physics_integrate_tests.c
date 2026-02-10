/**
 * @file p047_physics_integrate_tests.c
 * @brief Unit tests for Stage 12: Integrate + Sleep.
 *
 * Tests cover: position integration, gravity application, rotation
 * integration, static/kinematic skip, sleep detection, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/body.h"
#include "ferrum/physics/integrate.h"
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

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        printf("  %-50s", #fn);                                                 \
        int _r = fn();                                                          \
        printf("%s\n", _r ? "FAIL" : "PASS");                                   \
        if (_r) fail_count++;                                                   \
        test_count++;                                                           \
    } while (0)

/* ── Helpers ────────────────────────────────────────────────────── */

/** Create a dynamic body with identity orientation and given position/velocity. */
static phys_body_t make_dynamic_body(float px, float py, float pz,
                                     float vx, float vy, float vz,
                                     float mass)
{
    phys_body_t b;
    phys_body_init(&b);
    b.position    = (phys_vec3_t){px, py, pz};
    b.linear_vel  = (phys_vec3_t){vx, vy, vz};
    b.angular_vel = (phys_vec3_t){0.0f, 0.0f, 0.0f};
    b.orientation = (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
    b.flags       = 0;
    if (mass > 0.0f) {
        b.inv_mass = 1.0f / mass;
    } else {
        b.inv_mass = 0.0f;
    }
    b.sleep_counter = 0;
    return b;
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * Test 1: Body with vel=(1,0,0), dt=1.0 → position moves by (1,0,0).
 */
static int test_integrate_position(void)
{
    phys_body_t body_in = make_dynamic_body(0.0f, 0.0f, 0.0f,
                                            1.0f, 0.0f, 0.0f, 1.0f);
    phys_body_t body_out;
    /* Velocity from solver matches body velocity. */
    phys_velocity_t vel = {
        .linear  = {1.0f, 0.0f, 0.0f},
        .angular = {0.0f, 0.0f, 0.0f}
    };

    phys_integrate_args_t args = {
        .bodies_in             = &body_in,
        .velocities            = &vel,
        .bodies_out            = &body_out,
        .body_count            = 1,
        .dt                    = 1.0f,
        .gravity               = {0.0f, 0.0f, 0.0f},
        .sleep_threshold_linear  = 0.001f,
        .sleep_threshold_angular = 0.001f,
        .sleep_delay_frames    = 60
    };
    phys_stage_integrate(&args);

    ASSERT_FLOAT_NEAR(1.0f, body_out.position.x, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, body_out.position.y, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, body_out.position.z, 1e-5f);
    return 0;
}

/**
 * Test 2: Body at rest, gravity=(0,-9.81,0), dt=1/60 → velocity updated,
 * position moves downward.
 */
static int test_integrate_gravity(void)
{
    phys_body_t body_in = make_dynamic_body(0.0f, 10.0f, 0.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f);
    phys_body_t body_out;
    /* Gravity is pre-applied before this stage (e.g. in TGS init).
     * So the "solver output" velocity already includes the gravity step. */
    float dt = 1.0f / 60.0f;
    float expected_vy = -9.81f * dt;

    phys_velocity_t vel = {
        .linear  = {0.0f, expected_vy, 0.0f},
        .angular = {0.0f, 0.0f, 0.0f}
    };

    phys_integrate_args_t args = {
        .bodies_in             = &body_in,
        .velocities            = &vel,
        .bodies_out            = &body_out,
        .body_count            = 1,
        .dt                    = dt,
        .gravity               = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear  = 0.001f,
        .sleep_threshold_angular = 0.001f,
        .sleep_delay_frames    = 60
    };
    phys_stage_integrate(&args);

    ASSERT_FLOAT_NEAR(expected_vy, body_out.linear_vel.y, 1e-5f);

    /* Position should move: y = 10.0 + expected_vy * dt. */
    float expected_y = 10.0f + expected_vy * dt;
    ASSERT_FLOAT_NEAR(expected_y, body_out.position.y, 1e-4f);
    return 0;
}

/**
 * Test 3: Body with angular_vel=(0,0,PI), dt=1.0 → orientation changes.
 */
static int test_integrate_rotation(void)
{
    phys_body_t body_in = make_dynamic_body(0.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f);
    phys_body_t body_out;

    float pi = 3.14159265f;
    phys_velocity_t vel = {
        .linear  = {0.0f, 0.0f, 0.0f},
        .angular = {0.0f, 0.0f, pi}
    };

    phys_integrate_args_t args = {
        .bodies_in             = &body_in,
        .velocities            = &vel,
        .bodies_out            = &body_out,
        .body_count            = 1,
        .dt                    = 1.0f,
        .gravity               = {0.0f, 0.0f, 0.0f},
        .sleep_threshold_linear  = 0.001f,
        .sleep_threshold_angular = 0.001f,
        .sleep_delay_frames    = 60
    };
    phys_stage_integrate(&args);

    /* Orientation should no longer be identity. */
    float qx = body_out.orientation.x;
    float qy = body_out.orientation.y;
    float qz = body_out.orientation.z;
    float qw = body_out.orientation.w;
    float deviation = fabsf(qx) + fabsf(qy) + fabsf(qz) + fabsf(1.0f - qw);
    ASSERT_TRUE(deviation > 0.01f);

    /* Should be normalized. */
    float len = sqrtf(qx * qx + qy * qy + qz * qz + qw * qw);
    ASSERT_FLOAT_NEAR(1.0f, len, 1e-4f);
    return 0;
}

/**
 * Test 4: Static body (inv_mass=0) → position and velocity unchanged.
 */
static int test_integrate_static_unchanged(void)
{
    phys_body_t body_in = make_dynamic_body(5.0f, 5.0f, 5.0f,
                                            0.0f, 0.0f, 0.0f, 0.0f);
    /* inv_mass=0, not kinematic → static. */
    body_in.inv_mass = 0.0f;
    body_in.flags    = 0;

    phys_body_t body_out;
    phys_velocity_t vel = {
        .linear  = {10.0f, 10.0f, 10.0f},
        .angular = {1.0f, 1.0f, 1.0f}
    };

    phys_integrate_args_t args = {
        .bodies_in             = &body_in,
        .velocities            = &vel,
        .bodies_out            = &body_out,
        .body_count            = 1,
        .dt                    = 1.0f,
        .gravity               = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear  = 0.001f,
        .sleep_threshold_angular = 0.001f,
        .sleep_delay_frames    = 60
    };
    phys_stage_integrate(&args);

    /* Position unchanged. */
    ASSERT_FLOAT_NEAR(5.0f, body_out.position.x, 1e-5f);
    ASSERT_FLOAT_NEAR(5.0f, body_out.position.y, 1e-5f);
    ASSERT_FLOAT_NEAR(5.0f, body_out.position.z, 1e-5f);

    /* Velocity unchanged (still zero from copy). */
    ASSERT_FLOAT_NEAR(0.0f, body_out.linear_vel.x, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, body_out.linear_vel.y, 1e-5f);
    ASSERT_FLOAT_NEAR(0.0f, body_out.linear_vel.z, 1e-5f);
    return 0;
}

/**
 * Test 5: Kinematic body → not integrated.
 */
static int test_integrate_kinematic_unchanged(void)
{
    phys_body_t body_in = make_dynamic_body(3.0f, 3.0f, 3.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f);
    body_in.flags = PHYS_BODY_FLAG_KINEMATIC;

    phys_body_t body_out;
    phys_velocity_t vel = {
        .linear  = {5.0f, 5.0f, 5.0f},
        .angular = {1.0f, 1.0f, 1.0f}
    };

    phys_integrate_args_t args = {
        .bodies_in             = &body_in,
        .velocities            = &vel,
        .bodies_out            = &body_out,
        .body_count            = 1,
        .dt                    = 1.0f,
        .gravity               = {0.0f, -9.81f, 0.0f},
        .sleep_threshold_linear  = 0.001f,
        .sleep_threshold_angular = 0.001f,
        .sleep_delay_frames    = 60
    };
    phys_stage_integrate(&args);

    /* Position unchanged. */
    ASSERT_FLOAT_NEAR(3.0f, body_out.position.x, 1e-5f);
    ASSERT_FLOAT_NEAR(3.0f, body_out.position.y, 1e-5f);
    ASSERT_FLOAT_NEAR(3.0f, body_out.position.z, 1e-5f);
    return 0;
}

/**
 * Test 6: Body with very small velocity → after sleep_delay_frames+1
 * iterations, body is marked sleeping.
 */
static int test_integrate_sleep_detection(void)
{
    const uint32_t delay = 5;
    phys_body_t body_in = make_dynamic_body(0.0f, 0.0f, 0.0f,
                                            0.0f, 0.0f, 0.0f, 1.0f);
    phys_body_t body_out;

    /* Tiny velocity below threshold. */
    phys_velocity_t vel = {
        .linear  = {1e-5f, 0.0f, 0.0f},
        .angular = {0.0f, 0.0f, 0.0f}
    };

    phys_integrate_args_t args = {
        .bodies_in             = &body_in,
        .velocities            = &vel,
        .bodies_out            = &body_out,
        .body_count            = 1,
        .dt                    = 1.0f / 60.0f,
        .gravity               = {0.0f, 0.0f, 0.0f},
        .sleep_threshold_linear  = 0.01f,
        .sleep_threshold_angular = 0.01f,
        .sleep_delay_frames    = delay
    };

    /* Iterate delay+1 times, ping-ponging in/out. */
    for (uint32_t i = 0; i <= delay; ++i) {
        phys_stage_integrate(&args);
        /* Copy output back to input for next iteration. */
        body_in = body_out;
    }

    ASSERT_TRUE(body_out.sleep_counter >= delay);
    ASSERT_TRUE((body_out.flags & PHYS_BODY_FLAG_SLEEPING) != 0);
    return 0;
}

/**
 * Test 7: NULL args doesn't crash.
 */
static int test_integrate_null_safe(void)
{
    phys_stage_integrate(NULL);

    /* Also test with zero body_count. */
    phys_integrate_args_t args;
    memset(&args, 0, sizeof(args));
    phys_stage_integrate(&args);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p047_physics_integrate_tests\n");
    RUN_TEST(test_integrate_position);
    RUN_TEST(test_integrate_gravity);
    RUN_TEST(test_integrate_rotation);
    RUN_TEST(test_integrate_static_unchanged);
    RUN_TEST(test_integrate_kinematic_unchanged);
    RUN_TEST(test_integrate_sleep_detection);
    RUN_TEST(test_integrate_null_safe);

    printf("\n%d/%d tests passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
