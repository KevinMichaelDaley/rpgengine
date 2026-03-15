/**
 * @file entity_geometry_center_tests.c
 * @brief Tests for edit_entity_geometry_center() utility.
 *
 * Verifies pivot-to-geometry-center computation:
 *   geometry_center = pos + R * diag(scale) * (-pivot_offset)
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_entity_pivot.h"
#include "ferrum/math/quat.h"

static int tests_run = 0;
static int tests_passed = 0;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    tests_run++; \
    if (fn()) { tests_passed++; printf("OK   %s\n", #fn); } \
    else { printf("FAIL %s\n", #fn); } \
} while(0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabsf((float)(a)-(float)(b)) > (eps)) { \
        printf("  ASSERT_NEAR FAILED: %g != %g (line %d)\n", \
               (double)(a), (double)(b), __LINE__); return 0; } \
} while(0)

static edit_entity_t make_entity(float px, float py, float pz,
                                  float sx, float sy, float sz,
                                  quat_t orient,
                                  float pvx, float pvy, float pvz) {
    edit_entity_t e;
    memset(&e, 0, sizeof(e));
    e.pos[0] = px; e.pos[1] = py; e.pos[2] = pz;
    e.scale[0] = sx; e.scale[1] = sy; e.scale[2] = sz;
    e.orientation = orient;
    e.pivot_offset[0] = pvx; e.pivot_offset[1] = pvy; e.pivot_offset[2] = pvz;
    e.active = true;
    return e;
}

/** Zero pivot: geometry center == pos. */
static int test_zero_pivot(void) {
    edit_entity_t e = make_entity(5, 3, 7, 1, 1, 1,
                                   (quat_t){0, 0, 0, 1}, 0, 0, 0);
    float geo[3];
    edit_entity_geometry_center(&e, geo);
    ASSERT_NEAR(geo[0], 5.0f, 1e-5f);
    ASSERT_NEAR(geo[1], 3.0f, 1e-5f);
    ASSERT_NEAR(geo[2], 7.0f, 1e-5f);
    return 1;
}

/** Non-zero pivot, identity orientation, uniform scale=1. */
static int test_simple_pivot(void) {
    /* pos=(0,0,0), pivot=(1,0,0) → geometry center = (0,0,0) + I*I*(-1,0,0) = (-1,0,0) */
    edit_entity_t e = make_entity(0, 0, 0, 1, 1, 1,
                                   (quat_t){0, 0, 0, 1}, 1, 0, 0);
    float geo[3];
    edit_entity_geometry_center(&e, geo);
    ASSERT_NEAR(geo[0], -1.0f, 1e-5f);
    ASSERT_NEAR(geo[1], 0.0f, 1e-5f);
    ASSERT_NEAR(geo[2], 0.0f, 1e-5f);
    return 1;
}

/** Non-zero pivot with non-uniform scale. */
static int test_pivot_with_scale(void) {
    /* pos=(0,0,0), scale=(2,1,1), pivot=(1,0,0)
     * geo = pos + R * S * (-pivot) = (0,0,0) + I * (2*-1, 0, 0) = (-2,0,0) */
    edit_entity_t e = make_entity(0, 0, 0, 2, 1, 1,
                                   (quat_t){0, 0, 0, 1}, 1, 0, 0);
    float geo[3];
    edit_entity_geometry_center(&e, geo);
    ASSERT_NEAR(geo[0], -2.0f, 1e-5f);
    ASSERT_NEAR(geo[1], 0.0f, 1e-5f);
    ASSERT_NEAR(geo[2], 0.0f, 1e-5f);
    return 1;
}

/** Non-zero pivot with 90-degree Y rotation. */
static int test_pivot_with_rotation(void) {
    /* pos=(0,0,0), pivot=(1,0,0), 90 deg Y rotation.
     * R rotates X axis to -Z axis.
     * geo = pos + R * S * (-pivot) = R * (-1,0,0).
     * 90deg Y: (-1,0,0) → (0,0,1)
     * So geo = (0, 0, 1). */
    float half = 3.14159265358979323846f / 4.0f;
    quat_t rot90y = {0, sinf(half), 0, cosf(half)};
    edit_entity_t e = make_entity(0, 0, 0, 1, 1, 1, rot90y, 1, 0, 0);
    float geo[3];
    edit_entity_geometry_center(&e, geo);
    ASSERT_NEAR(geo[0], 0.0f, 0.01f);
    ASSERT_NEAR(geo[1], 0.0f, 0.01f);
    ASSERT_NEAR(geo[2], 1.0f, 0.01f);
    return 1;
}

/** Pivot + rotation + scale + non-origin pos. */
static int test_full_transform(void) {
    /* pos=(10,5,3), scale=(2,3,1), pivot=(1,1,0), 90 deg Y rotation.
     * scaled_neg_pivot = (-2, -3, 0)
     * R(90degY) * (-2,-3,0) = (0, -3, 2)
     * geo = (10,5,3) + (0,-3,2) = (10, 2, 5) */
    float half = 3.14159265358979323846f / 4.0f;
    quat_t rot90y = {0, sinf(half), 0, cosf(half)};
    edit_entity_t e = make_entity(10, 5, 3, 2, 3, 1, rot90y, 1, 1, 0);
    float geo[3];
    edit_entity_geometry_center(&e, geo);
    ASSERT_NEAR(geo[0], 10.0f, 0.02f);
    ASSERT_NEAR(geo[1], 2.0f, 0.02f);
    ASSERT_NEAR(geo[2], 5.0f, 0.02f);
    return 1;
}

int main(void) {
    RUN(test_zero_pivot);
    RUN(test_simple_pivot);
    RUN(test_pivot_with_scale);
    RUN(test_pivot_with_rotation);
    RUN(test_full_transform);

    printf("\n%d passed, %d failed\n", tests_passed, tests_run - tests_passed);
    return (tests_passed == tests_run) ? 0 : 1;
}
