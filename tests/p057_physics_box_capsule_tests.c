/**
 * @file p057_physics_box_capsule_tests.c
 * @brief Unit tests for phys-204: Box-Capsule Narrowphase.
 *
 * Tests cover: side contact, end-cap contact, separated, resting,
 * rotated capsule, penetrating, and NULL safety.
 */

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/collision/box_capsule.h"
#include "ferrum/physics/manifold.h"

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
 * Simplified: assumes axis is already unit length.
 */
static phys_quat_t quat_from_axis_angle_test(float ax, float ay, float az,
                                               float radians)
{
    float half = radians * 0.5f;
    float s = sinf(half);
    float c = cosf(half);
    return (phys_quat_t){ax * s, ay * s, az * s, c};
}

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * 1. Capsule alongside box, contacting a face.
 *
 * Unit box at origin (half_extents = 1,1,1).
 * Capsule upright at (1.4, 0, 0) with radius=0.5, half_height=1.
 * The capsule segment runs from (1.4, -1, 0) to (1.4, 1, 0).
 * Closest on segment to box: (1.4, 0, 0), clamped to box: (1, 0, 0).
 * Distance = 0.4, which is < radius 0.5 → contact.
 * Normal should point from capsule toward box face: (-1, 0, 0).
 * Penetration = 0.5 - 0.4 = 0.1.
 */
static int test_box_capsule_side(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){1.4f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.1f, contact.penetration, 0.01f);
    /* Normal from box (A) toward capsule (B): should be +X direction. */
    ASSERT_FLOAT_NEAR(1.0f, fabsf(contact.normal.x), 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    return 0;
}

/**
 * 2. Capsule end touching box face.
 *
 * Box at origin (half_extents = 1,1,1).
 * Capsule centered at (0, 2.3, 0) upright, radius=0.5, half_height=0.5.
 * Segment runs from (0, 1.8, 0) to (0, 2.8, 0).
 * Closest segment point to box: t=0 → (0, 1.8, 0).
 * Clamped to box: (0, 1, 0).
 * Distance = 0.8, but we need radius > distance for contact.
 *
 * Let's use capsule at (0, 1.9, 0): segment bottom at (0, 1.4, 0).
 * Clamped to box: (0, 1, 0). Distance = 0.4 < 0.5 → contact.
 * Normal: (0, 1, 0), penetration = 0.1.
 */
static int test_box_capsule_end_cap(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){0.0f, 1.9f, 0.0f}, QUAT_IDENTITY,
        0.5f, 0.5f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.1f, contact.penetration, 0.01f);
    /* Normal should be roughly +Y (from box toward capsule). */
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.x, 0.01f);
    ASSERT_FLOAT_NEAR(1.0f, fabsf(contact.normal.y), 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    return 0;
}

/**
 * 3. Far apart → no contact.
 *
 * Box at origin, capsule at (10, 0, 0).
 */
static int test_box_capsule_separated(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){10.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(!hit);
    return 0;
}

/**
 * 4. Capsule resting on top of box.
 *
 * Box at origin (half_extents = 1,1,1). Top face at y=1.
 * Capsule at (0, 1.5, 0) with radius=0.5, half_height=1.
 * The capsule's bottom hemisphere just touches the box top face.
 * Segment bottom at (0, 0.5, 0) → inside box!
 * Clamped to box: (0, 0.5, 0) → distance = 0 → deep contact.
 *
 * Instead: capsule at (0, 2.4, 0) with radius=0.5, half_height=1.
 * Segment bottom at (0, 1.4, 0).
 * Clamped to box: (0, 1, 0). Distance = 0.4 < 0.5 → contact.
 * Penetration = 0.1.
 */
static int test_box_capsule_resting(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){0.0f, 2.4f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.1f, contact.penetration, 0.01f);
    /* Normal should point upward (+Y from box toward capsule). */
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.x, 0.01f);
    ASSERT_FLOAT_NEAR(1.0f, fabsf(contact.normal.y), 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, contact.normal.z, 0.01f);
    return 0;
}

/**
 * 5. Capsule at 45° angle contacting box edge.
 *
 * Box at origin (half_extents = 1,1,1).
 * Capsule rotated 90° around Z (lies along X axis), centered at (2.3, 0, 0).
 * radius=0.5, half_height=1.
 * Segment: (1.3, 0, 0) to (3.3, 0, 0).
 * Closest to box: t=0 → (1.3, 0, 0).
 * Clamped to box: (1, 0, 0). Distance = 0.3 < 0.5 → contact.
 */
static int test_box_capsule_rotated(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    /* Rotate capsule 90° around Z → axis becomes +X. */
    phys_quat_t capsule_rot = quat_from_axis_angle_test(
        0.0f, 0.0f, 1.0f, (float)(M_PI / 2.0));

    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){2.3f, 0.0f, 0.0f}, capsule_rot,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.2f, contact.penetration, 0.02f);
    /* Normal should be roughly +X. */
    ASSERT_FLOAT_NEAR(1.0f, fabsf(contact.normal.x), 0.05f);
    return 0;
}

/**
 * 6. Capsule pushed into box (deep penetration).
 *
 * Box at origin (half_extents = 1,1,1).
 * Capsule at (0.5, 0, 0) upright, radius=0.5, half_height=1.
 * The capsule's center is inside the box → deep penetration.
 * Should still return true with positive penetration.
 */
static int test_box_capsule_penetrating(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){0.5f, 0.0f, 0.0f}, QUAT_IDENTITY,
        0.5f, 1.0f,
        0.0f, &contact);

    ASSERT_TRUE(hit);
    ASSERT_TRUE(contact.penetration > 0.0f);
    /* Normal magnitude should be ~1 (unit vector). */
    float mag = sqrtf(contact.normal.x * contact.normal.x
                      + contact.normal.y * contact.normal.y
                      + contact.normal.z * contact.normal.z);
    ASSERT_FLOAT_NEAR(1.0f, mag, 0.01f);
    return 0;
}

/**
 * 7. NULL contact_out doesn't crash.
 */
static int test_box_capsule_null_safe(void)
{
    bool hit = phys_box_vs_capsule(
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        (phys_vec3_t){1.4f, 0.0f, 0.0f}, QUAT_IDENTITY,
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

    printf("p057_physics_box_capsule_tests\n");

    RUN_TEST(test_box_capsule_side);
    RUN_TEST(test_box_capsule_end_cap);
    RUN_TEST(test_box_capsule_separated);
    RUN_TEST(test_box_capsule_resting);
    RUN_TEST(test_box_capsule_rotated);
    RUN_TEST(test_box_capsule_penetrating);
    RUN_TEST(test_box_capsule_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
