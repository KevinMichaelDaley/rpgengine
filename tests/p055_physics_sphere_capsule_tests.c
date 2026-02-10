/**
 * @file p055_physics_sphere_capsule_tests.c
 * @brief Unit tests for sphere-capsule narrowphase (phys-202).
 *
 * Tests: side contact, top cap, bottom cap, separated, touching,
 * rotated capsule, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "ferrum/physics/manifold.h"
#include "ferrum/physics/narrowphase.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FALSE(cond) ASSERT_TRUE(!(cond))

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

/** Identity quaternion. */
static phys_quat_t quat_identity(void)
{
    return (phys_quat_t){0.0f, 0.0f, 0.0f, 1.0f};
}

/** Build a quaternion rotating 90 degrees around the Z axis. */
static phys_quat_t quat_rot_z_90(void)
{
    /* 90° around Z: half-angle = 45°, sin(45°) ≈ 0.7071, cos(45°) ≈ 0.7071 */
    float s = 0.70710678118f;
    return (phys_quat_t){0.0f, 0.0f, s, s};
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * test_sphere_capsule_side
 * Sphere at (3, 0, 0), radius 1. Capsule at origin, identity rotation,
 * radius 1, half_height 2. Closest point on segment is (0,0,0).
 * Distance = 3, sum radii = 2, so gap = 1 → no contact.
 *
 * Move sphere to (1.5, 0, 0) → distance = 1.5, sum radii = 2,
 * penetration = 0.5.
 */
static int test_sphere_capsule_side(void)
{
    phys_vec3_t sphere_center = {1.5f, 0.0f, 0.0f};
    float sphere_radius = 1.0f;
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_identity();
    float capsule_radius = 1.0f;
    float capsule_half_height = 2.0f;

    phys_contact_point_t contact;
    bool hit = phys_sphere_vs_capsule(
        sphere_center, sphere_radius,
        capsule_center, capsule_rot,
        capsule_radius, capsule_half_height,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Normal should point from capsule toward sphere: roughly +X. */
    ASSERT_FLOAT_NEAR(1.0f, contact.normal.x, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    /* Penetration = (1 + 1) - 1.5 = 0.5. */
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.01f);
    return 0;
}

/**
 * test_sphere_capsule_top_cap
 * Sphere above the capsule top cap. Capsule at origin, half_height=2,
 * radius=1. Top endpoint is (0,2,0). Sphere at (0,3.5,0), radius=1.
 * Closest point on segment: (0,2,0). Distance = 1.5, sum radii = 2,
 * penetration = 0.5.
 */
static int test_sphere_capsule_top_cap(void)
{
    phys_vec3_t sphere_center = {0.0f, 3.5f, 0.0f};
    float sphere_radius = 1.0f;
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_identity();
    float capsule_radius = 1.0f;
    float capsule_half_height = 2.0f;

    phys_contact_point_t contact;
    bool hit = phys_sphere_vs_capsule(
        sphere_center, sphere_radius,
        capsule_center, capsule_rot,
        capsule_radius, capsule_half_height,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Normal should point upward (+Y). */
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.x, 0.01f);
    ASSERT_FLOAT_NEAR(1.0f, contact.normal.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.01f);
    return 0;
}

/**
 * test_sphere_capsule_bottom_cap
 * Sphere below the capsule bottom cap. Bottom endpoint is (0,-2,0).
 * Sphere at (0,-3.5,0), radius=1. Distance = 1.5, sum radii = 2,
 * penetration = 0.5.
 */
static int test_sphere_capsule_bottom_cap(void)
{
    phys_vec3_t sphere_center = {0.0f, -3.5f, 0.0f};
    float sphere_radius = 1.0f;
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_identity();
    float capsule_radius = 1.0f;
    float capsule_half_height = 2.0f;

    phys_contact_point_t contact;
    bool hit = phys_sphere_vs_capsule(
        sphere_center, sphere_radius,
        capsule_center, capsule_rot,
        capsule_radius, capsule_half_height,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Normal should point downward (-Y). */
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.x, 0.01f);
    ASSERT_FLOAT_NEAR(-1.0f, contact.normal.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.01f);
    return 0;
}

/**
 * test_sphere_capsule_separated
 * Sphere far away → no contact.
 */
static int test_sphere_capsule_separated(void)
{
    phys_vec3_t sphere_center = {10.0f, 0.0f, 0.0f};
    float sphere_radius = 1.0f;
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_identity();
    float capsule_radius = 1.0f;
    float capsule_half_height = 2.0f;

    phys_contact_point_t contact;
    bool hit = phys_sphere_vs_capsule(
        sphere_center, sphere_radius,
        capsule_center, capsule_rot,
        capsule_radius, capsule_half_height,
        0.0f, &contact);

    ASSERT_FALSE(hit);
    return 0;
}

/**
 * test_sphere_capsule_touching
 * Sphere exactly touching the capsule side. Sphere at (2, 0, 0), radius 1.
 * Capsule radius 1. Distance to segment = 2, sum radii = 2 → penetration ≈ 0.
 */
static int test_sphere_capsule_touching(void)
{
    phys_vec3_t sphere_center = {2.0f, 0.0f, 0.0f};
    float sphere_radius = 1.0f;
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_identity();
    float capsule_radius = 1.0f;
    float capsule_half_height = 2.0f;

    phys_contact_point_t contact;
    bool hit = phys_sphere_vs_capsule(
        sphere_center, sphere_radius,
        capsule_center, capsule_rot,
        capsule_radius, capsule_half_height,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.0f, contact.penetration, 0.01f);
    return 0;
}

/**
 * test_sphere_capsule_rotated
 * Capsule rotated 90° around Z → axis is now along +X.
 * Endpoints: center ± (half_height, 0, 0).
 * Capsule at origin, half_height=2, radius=1.
 * Endpoints: (-2,0,0) and (2,0,0).
 * Sphere at (0, 1.5, 0), radius=1.
 * Closest point on segment: (0,0,0). Distance = 1.5.
 * Sum radii = 2. Penetration = 0.5.
 */
static int test_sphere_capsule_rotated(void)
{
    phys_vec3_t sphere_center = {0.0f, 1.5f, 0.0f};
    float sphere_radius = 1.0f;
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_rot_z_90();
    float capsule_radius = 1.0f;
    float capsule_half_height = 2.0f;

    phys_contact_point_t contact;
    bool hit = phys_sphere_vs_capsule(
        sphere_center, sphere_radius,
        capsule_center, capsule_rot,
        capsule_radius, capsule_half_height,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Normal should point upward (+Y) from capsule toward sphere. */
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.x, 0.01f);
    ASSERT_FLOAT_NEAR(1.0f, contact.normal.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.01f);
    return 0;
}

/**
 * test_sphere_capsule_null_safe
 * Passing NULL contact_out must return false without crashing.
 */
static int test_sphere_capsule_null_safe(void)
{
    phys_vec3_t sphere_center = {1.0f, 0.0f, 0.0f};
    phys_vec3_t capsule_center = {0.0f, 0.0f, 0.0f};
    phys_quat_t capsule_rot = quat_identity();

    bool hit = phys_sphere_vs_capsule(
        sphere_center, 1.0f,
        capsule_center, capsule_rot,
        1.0f, 2.0f,
        0.0f, NULL);

    ASSERT_FALSE(hit);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p055_physics_sphere_capsule_tests\n");
    RUN_TEST(test_sphere_capsule_side);
    RUN_TEST(test_sphere_capsule_top_cap);
    RUN_TEST(test_sphere_capsule_bottom_cap);
    RUN_TEST(test_sphere_capsule_separated);
    RUN_TEST(test_sphere_capsule_touching);
    RUN_TEST(test_sphere_capsule_rotated);
    RUN_TEST(test_sphere_capsule_null_safe);

    printf("\n%d/%d tests passed.\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
