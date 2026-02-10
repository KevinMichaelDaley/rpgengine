/**
 * @file p058_physics_capsule_capsule_tests.c
 * @brief Unit tests for capsule vs capsule narrowphase collision.
 *
 * Tests cover: parallel, perpendicular, end-to-end, separated,
 * colinear, touching, and NULL safety.
 */

#define _USE_MATH_DEFINES
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include "ferrum/physics/collision/capsule_capsule.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

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

#define ASSERT_VEC3_NEAR(exp, act, tol)                                        \
    do {                                                                        \
        phys_vec3_t _ev = (exp), _av = (act);                                  \
        float _t = (tol);                                                      \
        if (fabsf(_ev.x - _av.x) > _t || fabsf(_ev.y - _av.y) > _t           \
            || fabsf(_ev.z - _av.z) > _t) {                                    \
            fprintf(stderr, "ASSERT_VEC3_NEAR failed: %s:%d: "                \
                    "expected (%.4f,%.4f,%.4f) got (%.4f,%.4f,%.4f)\n",         \
                    __FILE__, __LINE__,                                         \
                    (double)_ev.x, (double)_ev.y, (double)_ev.z,               \
                    (double)_av.x, (double)_av.y, (double)_av.z);              \
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
static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};

/**
 * @brief Build a quaternion from axis-angle for test setup.
 *
 * Wraps quat_from_axis_angle with a small epsilon.
 */
static phys_quat_t make_rotation(float ax, float ay, float az, float radians)
{
    vec3_t axis = {ax, ay, az};
    return quat_from_axis_angle(axis, radians, 1e-6f);
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * 1. Two parallel vertical capsules side by side along X.
 *    Both upright (identity rotation), radius=0.5, half_height=1.0.
 *    A at origin, B at (1.5, 0, 0).
 *    Closest segments are parallel; closest distance = 1.5.
 *    Combined radius = 1.0.  Overlap = 1.0 - 1.5 < 0?  No, wait.
 *    Actually distance between axes = 1.5, combined radius = 1.0,
 *    so they don't overlap.  Let's place B closer.
 *    B at (0.8, 0, 0): distance = 0.8, combined = 1.0, pen = 0.2.
 */
static int test_capsule_capsule_parallel(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    /* Two vertical capsules, radius=0.5, half_height=1.0.
     * A at origin, B at (0.8, 0, 0). Parallel segments.
     * Closest distance between segments = 0.8 (axes are parallel along Y).
     * Combined radius = 1.0. Penetration = 0.2. */
    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        (phys_vec3_t){0.8f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.2f, contact.penetration, 0.01f);
    /* Normal should point from A to B along +X. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 2. Two capsules at 90°: A vertical (identity), B rotated 90° around Z.
 *    Both radius=0.5, half_height=1.0.
 *    A at origin, B at (0.0, 0.0, 0.0) — crossing at the center.
 *    Closest points are both at center. dist=0 → fallback normal.
 *    Penetration = combined radius = 1.0.
 */
static int test_capsule_capsule_perpendicular(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    /* B rotated 90° around Z axis: its +Y axis becomes +X. */
    phys_quat_t rot_z90 = make_rotation(0.0f, 0.0f, 1.0f, (float)(M_PI / 2.0));

    /* Both at origin, perpendicular. Closest points both at (0,0,0). */
    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, rot_z90,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Closest points coincide at origin → fallback normal (0,1,0). */
    ASSERT_FLOAT_NEAR(1.0f, contact.penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 1.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 3. End-to-end: capsule A at origin (vertical), capsule B directly above.
 *    A: center=(0,0,0), half_height=1.0, radius=0.5 → top at y=1.0
 *    B: center=(0,2.5,0), half_height=1.0, radius=0.5 → bottom at y=1.5
 *    Closest points: top of A = (0,1,0), bottom of B = (0,1.5,0).
 *    dist = 0.5, combined_radius = 1.0, pen = 0.5.
 */
static int test_capsule_capsule_end_to_end(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        (phys_vec3_t){0.0f, 2.5f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.01f);
    /* Normal from A toward B = +Y. */
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 1.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 4. Far apart → no contact.
 *    A at origin, B at (10, 0, 0). Both radius=0.5, half_height=1.0.
 *    Min distance between axes = 10.0, combined_radius = 1.0 → no overlap.
 */
static int test_capsule_capsule_separated(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        (phys_vec3_t){10.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(!hit);
    return 0;
}

/**
 * 5. Colinear: both on the Y axis, overlapping end-to-end.
 *    A: center=(0,0,0), half_height=1.0, radius=0.3 → segment [-1, 1]
 *    B: center=(0,1.5,0), half_height=1.0, radius=0.3 → segment [0.5, 2.5]
 *    Segments are parallel/colinear. The closest-point algorithm
 *    resolves to the overlap boundary: both closest points land at
 *    (0,0.5,0), so dist=0 → fallback normal (0,1,0), pen = 0.6.
 */
static int test_capsule_capsule_colinear(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.3f, 1.0f,
        (phys_vec3_t){0.0f, 1.5f, 0.0f}, QUAT_IDENTITY,
        0.3f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    /* Colinear segments overlap → closest points coincide → fallback. */
    ASSERT_FLOAT_NEAR(0.6f, contact.penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 1.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 6. Just barely touching: penetration ≈ 0.
 *    A at origin, B at (2.0, 0, 0). Both radius=1.0, half_height=0.5.
 *    Parallel vertical capsules. Axis distance = 2.0.
 *    Combined radius = 2.0. Penetration = 0.
 */
static int test_capsule_capsule_touching(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        1.0f, 0.5f,
        (phys_vec3_t){2.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        1.0f, 0.5f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.0f, contact.penetration, 0.01f);
    return 0;
}

/**
 * 7. NULL contact_out returns false without crashing.
 */
static int test_capsule_capsule_null_safe(void)
{
    bool hit = phys_capsule_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, NULL);

    ASSERT_TRUE(!hit);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p058_physics_capsule_capsule_tests\n");

    RUN_TEST(test_capsule_capsule_parallel);
    RUN_TEST(test_capsule_capsule_perpendicular);
    RUN_TEST(test_capsule_capsule_end_to_end);
    RUN_TEST(test_capsule_capsule_separated);
    RUN_TEST(test_capsule_capsule_colinear);
    RUN_TEST(test_capsule_capsule_touching);
    RUN_TEST(test_capsule_capsule_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
