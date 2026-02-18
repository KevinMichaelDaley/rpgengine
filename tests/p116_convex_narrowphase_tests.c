/**
 * @file p116_convex_narrowphase_tests.c
 * @brief Tests for primitive-vs-convex narrowphase dispatch.
 *
 * Tests cover:
 *   1. Sphere vs convex hull — overlapping
 *   2. Sphere vs convex hull — separated
 *   3. Box vs convex hull — overlapping
 *   4. Box vs convex hull — separated
 *   5. Capsule vs convex hull — overlapping
 *   6. Capsule vs convex hull — separated
 *   7. Convex vs convex — overlapping
 *   8. Convex vs convex — separated
 *   9. NULL safety
 *  10. Speculative contact (separated within margin)
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/physics/narrowphase_convex.h"
#include "ferrum/physics/convex_hull.h"

/* ── Test harness ──────────────────────────────────────────────── */

static int test_count;
static int fail_count;

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                        \
        if (!(cond)) {                                                          \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",                 \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FALSE(cond)                                                     \
    do {                                                                        \
        if ((cond)) {                                                           \
            fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n",                \
                    __FILE__, __LINE__, #cond);                                 \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                        \
        float _e = (exp), _a = (act);                                           \
        if (fabsf(_e - _a) > (eps)) {                                           \
            fprintf(stderr,                                                     \
                    "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f, got %f "     \
                    "(eps=%f)\n",                                               \
                    __FILE__, __LINE__, (double)_e, (double)_a, (double)(eps)); \
            return 1;                                                           \
        }                                                                       \
    } while (0)

#define RUN_TEST(fn)                                                           \
    do {                                                                        \
        test_count++;                                                           \
        int _r = fn();                                                          \
        if (_r) {                                                               \
            fail_count++;                                                       \
            fprintf(stderr, "  FAIL: %s\n", #fn);                              \
        } else {                                                                \
            fprintf(stderr, "  PASS: %s\n", #fn);                              \
        }                                                                       \
    } while (0)

/* ── Helper: build a unit cube hull centered at origin ─────────── */

static void build_unit_cube(phys_convex_hull_t *hull) {
    phys_vec3_t pts[8] = {
        {-0.5f, -0.5f, -0.5f}, { 0.5f, -0.5f, -0.5f},
        { 0.5f,  0.5f, -0.5f}, {-0.5f,  0.5f, -0.5f},
        {-0.5f, -0.5f,  0.5f}, { 0.5f, -0.5f,  0.5f},
        { 0.5f,  0.5f,  0.5f}, {-0.5f,  0.5f,  0.5f},
    };
    memset(hull, 0, sizeof(*hull));
    phys_convex_hull_build(hull, pts, 8);
}

static const phys_quat_t QUAT_ID = {0, 0, 0, 1};

/* ── Tests ─────────────────────────────────────────────────────── */

/** 1. Sphere overlapping convex hull. */
static int test_sphere_vs_convex_overlap(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Sphere center at origin overlaps unit cube centered at origin. */
    bool hit = phys_sphere_vs_convex(
        (phys_vec3_t){0, 0, 0}, 0.3f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, &contact);
    ASSERT_TRUE(hit);
    ASSERT_TRUE(contact.penetration > 0.0f);
    return 0;
}

/** 2. Sphere separated from convex hull. */
static int test_sphere_vs_convex_separated(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Sphere at x=3 well separated from cube at origin. */
    bool hit = phys_sphere_vs_convex(
        (phys_vec3_t){3.0f, 0, 0}, 0.3f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, &contact);
    ASSERT_FALSE(hit);
    return 0;
}

/** 3. Box overlapping convex hull. */
static int test_box_vs_convex_overlap(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Box at x=0.4 overlaps cube at origin (both half-extent=0.5). */
    bool hit = phys_box_vs_convex(
        (phys_vec3_t){0.4f, 0, 0}, QUAT_ID,
        (phys_vec3_t){0.5f, 0.5f, 0.5f},
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, &contact);
    ASSERT_TRUE(hit);
    ASSERT_TRUE(contact.penetration > 0.0f);
    return 0;
}

/** 4. Box separated from convex hull. */
static int test_box_vs_convex_separated(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Box at x=5 well separated. */
    bool hit = phys_box_vs_convex(
        (phys_vec3_t){5.0f, 0, 0}, QUAT_ID,
        (phys_vec3_t){0.5f, 0.5f, 0.5f},
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, &contact);
    ASSERT_FALSE(hit);
    return 0;
}

/** 5. Capsule overlapping convex hull. */
static int test_capsule_vs_convex_overlap(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Capsule at origin overlaps cube at origin. */
    bool hit = phys_capsule_vs_convex(
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        0.2f, 0.3f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, &contact);
    ASSERT_TRUE(hit);
    ASSERT_TRUE(contact.penetration > 0.0f);
    return 0;
}

/** 6. Capsule separated from convex hull. */
static int test_capsule_vs_convex_separated(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Capsule at x=5 well separated. */
    bool hit = phys_capsule_vs_convex(
        (phys_vec3_t){5.0f, 0, 0}, QUAT_ID,
        0.2f, 0.3f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, &contact);
    ASSERT_FALSE(hit);
    return 0;
}

/** 7. Convex vs convex overlapping. */
static int test_convex_vs_convex_overlap(void) {
    phys_convex_hull_t hull_a, hull_b;
    build_unit_cube(&hull_a);
    build_unit_cube(&hull_b);

    phys_contact_point_t contact;
    /* Two unit cubes offset by 0.4 along X — overlap of 0.6. */
    bool hit = phys_convex_vs_convex(
        (phys_vec3_t){0, 0, 0}, QUAT_ID, &hull_a,
        (phys_vec3_t){0.4f, 0, 0}, QUAT_ID, &hull_b,
        0.0f, &contact);
    ASSERT_TRUE(hit);
    ASSERT_TRUE(contact.penetration > 0.0f);
    /* Penetration should be approximately 0.6 (1.0 - 0.4). */
    ASSERT_FLOAT_NEAR(0.6f, contact.penetration, 0.15f);
    return 0;
}

/** 8. Convex vs convex separated. */
static int test_convex_vs_convex_separated(void) {
    phys_convex_hull_t hull_a, hull_b;
    build_unit_cube(&hull_a);
    build_unit_cube(&hull_b);

    phys_contact_point_t contact;
    /* Two unit cubes offset by 3.0 along X — well separated. */
    bool hit = phys_convex_vs_convex(
        (phys_vec3_t){0, 0, 0}, QUAT_ID, &hull_a,
        (phys_vec3_t){3.0f, 0, 0}, QUAT_ID, &hull_b,
        0.0f, &contact);
    ASSERT_FALSE(hit);
    return 0;
}

/** 9. NULL safety — should not crash. */
static int test_null_safety(void) {
    phys_contact_point_t contact;
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    /* NULL hull. */
    ASSERT_FALSE(phys_sphere_vs_convex(
        (phys_vec3_t){0, 0, 0}, 1.0f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        NULL, 0.0f, &contact));

    /* NULL contact_out. */
    ASSERT_FALSE(phys_sphere_vs_convex(
        (phys_vec3_t){0, 0, 0}, 1.0f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.0f, NULL));

    /* NULL hulls for convex_vs_convex. */
    ASSERT_FALSE(phys_convex_vs_convex(
        (phys_vec3_t){0, 0, 0}, QUAT_ID, NULL,
        (phys_vec3_t){0, 0, 0}, QUAT_ID, &hull,
        0.0f, &contact));
    ASSERT_FALSE(phys_convex_vs_convex(
        (phys_vec3_t){0, 0, 0}, QUAT_ID, &hull,
        (phys_vec3_t){0, 0, 0}, QUAT_ID, NULL,
        0.0f, &contact));

    return 0;
}

/** 10. Speculative contact — sphere near but not overlapping convex. */
static int test_speculative_contact(void) {
    phys_convex_hull_t hull;
    build_unit_cube(&hull);

    phys_contact_point_t contact;
    /* Sphere at x=1.0, radius=0.3 → gap of ~0.2 from cube face at x=0.5. */
    bool hit = phys_sphere_vs_convex(
        (phys_vec3_t){1.0f, 0, 0}, 0.3f,
        (phys_vec3_t){0, 0, 0}, QUAT_ID,
        &hull, 0.5f, &contact);  /* speculative margin = 0.5 */
    ASSERT_TRUE(hit);
    ASSERT_TRUE(contact.penetration < 0.0f);  /* Negative = speculative. */
    return 0;
}

/* ── Main ──────────────────────────────────────────────────────── */

int main(void) {
    fprintf(stderr, "=== p116_convex_narrowphase_tests ===\n");

    RUN_TEST(test_sphere_vs_convex_overlap);
    RUN_TEST(test_sphere_vs_convex_separated);
    RUN_TEST(test_box_vs_convex_overlap);
    RUN_TEST(test_box_vs_convex_separated);
    RUN_TEST(test_capsule_vs_convex_overlap);
    RUN_TEST(test_capsule_vs_convex_separated);
    RUN_TEST(test_convex_vs_convex_overlap);
    RUN_TEST(test_convex_vs_convex_separated);
    RUN_TEST(test_null_safety);
    RUN_TEST(test_speculative_contact);

    fprintf(stderr, "\n%d/%d passed\n", test_count - fail_count, test_count);
    return fail_count ? 1 : 0;
}
