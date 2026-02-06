/**
 * @file p054_physics_sphere_box_tests.c
 * @brief Unit tests for sphere-box narrowphase collision detection.
 *
 * Tests cover: face contact, edge contact, corner contact, separated,
 * resting on top, penetrating, sphere inside box, and NULL safety.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/** Identity quaternion (no rotation). */
static const phys_quat_t QUAT_IDENTITY = {0.0f, 0.0f, 0.0f, 1.0f};

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * 1. Face contact: sphere at (2,0,0) r=1, box at origin half=(1,1,1).
 *    Sphere just touching +X face. Penetration ≈ 0, normal ≈ (+1,0,0).
 */
static int test_sphere_box_face_contact(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){2.0f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.0f, contact.penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 2. Edge contact: sphere near box edge where two faces meet.
 *    Sphere at (2, 2, 0) r=1, box at origin half=(1,1,1).
 *    Closest point on box is edge at (1,1,0), distance = sqrt(2) ≈ 1.414.
 *    Since r=1 < sqrt(2), they're separated. Use r=1.0 sphere at (1.5, 1.5, 0).
 *    Closest = (1,1,0), diff = (0.5,0.5,0), dist = sqrt(0.5) ≈ 0.707.
 *    Penetration = 1.0 - 0.707 ≈ 0.293.
 */
static int test_sphere_box_edge_contact(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){1.5f, 1.5f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(hit);
    /* dist = sqrt(0.5^2 + 0.5^2) = sqrt(0.5) ≈ 0.7071 */
    float expected_pen = 1.0f - sqrtf(0.5f);
    ASSERT_FLOAT_NEAR(expected_pen, contact.penetration, 0.01f);
    /* Normal should point along (1,1,0) normalized = (0.707, 0.707, 0) */
    float inv_sqrt2 = 1.0f / sqrtf(2.0f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){inv_sqrt2, inv_sqrt2, 0.0f}),
                     contact.normal, 0.01f);
    return 0;
}

/**
 * 3. Corner contact: sphere near box corner.
 *    Sphere at (1.5, 1.5, 1.5) r=1, box at origin half=(1,1,1).
 *    Closest point = (1,1,1), diff = (0.5,0.5,0.5), dist = sqrt(0.75) ≈ 0.866.
 *    Penetration = 1.0 - 0.866 ≈ 0.134.
 */
static int test_sphere_box_corner_contact(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){1.5f, 1.5f, 1.5f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(hit);
    float expected_pen = 1.0f - sqrtf(0.75f);
    ASSERT_FLOAT_NEAR(expected_pen, contact.penetration, 0.01f);
    /* Normal should point along (1,1,1) normalized */
    float inv_sqrt3 = 1.0f / sqrtf(3.0f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){inv_sqrt3, inv_sqrt3, inv_sqrt3}),
                     contact.normal, 0.01f);
    return 0;
}

/**
 * 4. Separated: sphere far from box → no contact.
 *    Sphere at (5,0,0) r=1, box at origin half=(1,1,1). Gap = 3.
 */
static int test_sphere_box_separated(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){5.0f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(!hit);
    return 0;
}

/**
 * 5. Resting on top: sphere at (0,2,0) r=1, box half=(1,1,1).
 *    Sphere just touching +Y face. Penetration ≈ 0, normal ≈ (0,1,0).
 */
static int test_sphere_box_resting_on_top(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){0.0f, 2.0f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.0f, contact.penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){0.0f, 1.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 6. Penetrating: sphere overlapping box significantly.
 *    Sphere at (1.5, 0, 0) r=1, box at origin half=(1,1,1).
 *    Closest on box = (1,0,0), diff = (0.5,0,0), dist = 0.5.
 *    Penetration = 1.0 - 0.5 = 0.5.
 */
static int test_sphere_box_penetrating(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){1.5f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.5f, contact.penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 7. Sphere inside box: sphere center inside box, pushed out along closest face.
 *    Sphere at (0.5, 0, 0) r=0.25, box at origin half=(1,1,1).
 *    Center is inside box. Closest face is +X at distance 0.5.
 *    Penetration = half_extent_x - |local_x| + radius = 1.0 - 0.5 + 0.25 = 0.75.
 *    Normal = (+1, 0, 0).
 */
static int test_sphere_inside_box(void)
{
    phys_contact_point_t contact;
    memset(&contact, 0, sizeof(contact));

    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){0.5f, 0.0f, 0.0f}, 0.25f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        &contact);

    ASSERT_TRUE(hit);
    ASSERT_FLOAT_NEAR(0.75f, contact.penetration, 0.01f);
    ASSERT_VEC3_NEAR(((phys_vec3_t){1.0f, 0.0f, 0.0f}), contact.normal, 0.01f);
    return 0;
}

/**
 * 8. NULL contact_out doesn't crash, returns false.
 */
static int test_sphere_box_null_safe(void)
{
    bool hit = phys_sphere_vs_box(
        (phys_vec3_t){1.5f, 0.0f, 0.0f}, 1.0f,
        (phys_vec3_t){0.0f, 0.0f, 0.0f}, QUAT_IDENTITY,
        (phys_vec3_t){1.0f, 1.0f, 1.0f},
        NULL);

    ASSERT_TRUE(!hit);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void)
{
    int test_count = 0;
    int fail_count = 0;

    printf("p054_physics_sphere_box_tests\n");

    RUN_TEST(test_sphere_box_face_contact);
    RUN_TEST(test_sphere_box_edge_contact);
    RUN_TEST(test_sphere_box_corner_contact);
    RUN_TEST(test_sphere_box_separated);
    RUN_TEST(test_sphere_box_resting_on_top);
    RUN_TEST(test_sphere_box_penetrating);
    RUN_TEST(test_sphere_inside_box);
    RUN_TEST(test_sphere_box_null_safe);

    printf("\n  %d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
