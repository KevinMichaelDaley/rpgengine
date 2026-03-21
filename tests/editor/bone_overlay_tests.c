/**
 * @file bone_overlay_tests.c
 * @brief Tests for bone capsule geometry computation.
 *
 * Validates the bone_capsule_params_from_joint() helper that computes
 * capsule center, axis, length, and radius from joint head/tail positions.
 */

#include "ferrum/editor/scene/scene_viewport_bone_overlay.h"
#include "ferrum/math/vec3.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_pass, g_fail;

#define ASSERT(cond) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; \
           fprintf(stderr, "  FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); } \
} while (0)

#define ASSERT_NEAR(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

/* ---- Tests ---- */

/** Simple vertical bone: head at origin, tail at (0,1,0). */
static void test_vertical_bone(void) {
    float head[3] = {0.0f, 0.0f, 0.0f};
    float tail[3] = {0.0f, 1.0f, 0.0f};

    bone_capsule_params_t params;
    bone_capsule_params_from_joint(head, tail, &params);

    /* Center should be midpoint. */
    ASSERT_NEAR(params.center[0], 0.0f, 0.001f);
    ASSERT_NEAR(params.center[1], 0.5f, 0.001f);
    ASSERT_NEAR(params.center[2], 0.0f, 0.001f);

    /* Length should be 1.0. */
    ASSERT_NEAR(params.length, 1.0f, 0.001f);

    /* Radius proportional to length. */
    ASSERT(params.radius > 0.0f);
    ASSERT(params.radius < params.length * 0.5f);
}

/** Diagonal bone: head at (1,1,1), tail at (2,2,2). */
static void test_diagonal_bone(void) {
    float head[3] = {1.0f, 1.0f, 1.0f};
    float tail[3] = {2.0f, 2.0f, 2.0f};

    bone_capsule_params_t params;
    bone_capsule_params_from_joint(head, tail, &params);

    /* Center should be midpoint (1.5, 1.5, 1.5). */
    ASSERT_NEAR(params.center[0], 1.5f, 0.001f);
    ASSERT_NEAR(params.center[1], 1.5f, 0.001f);
    ASSERT_NEAR(params.center[2], 1.5f, 0.001f);

    /* Length should be sqrt(3) ≈ 1.732. */
    ASSERT_NEAR(params.length, sqrtf(3.0f), 0.01f);
}

/** Zero-length bone (head == tail) produces zero length and min radius. */
static void test_zero_length_bone(void) {
    float head[3] = {5.0f, 5.0f, 5.0f};
    float tail[3] = {5.0f, 5.0f, 5.0f};

    bone_capsule_params_t params;
    bone_capsule_params_from_joint(head, tail, &params);

    ASSERT_NEAR(params.length, 0.0f, 0.001f);
    /* Should use minimum radius for zero-length bones. */
    ASSERT(params.radius >= BONE_CAPSULE_MIN_RADIUS);
}

/** Very long bone caps radius at maximum. */
static void test_long_bone_radius_cap(void) {
    float head[3] = {0.0f, 0.0f, 0.0f};
    float tail[3] = {0.0f, 100.0f, 0.0f};

    bone_capsule_params_t params;
    bone_capsule_params_from_joint(head, tail, &params);

    ASSERT(params.radius <= BONE_CAPSULE_MAX_RADIUS);
}

/* ---- Main ---- */

int main(void) {
    printf("bone_overlay_tests:\n");

    test_vertical_bone();
    test_diagonal_bone();
    test_zero_length_bone();
    test_long_bone_radius_cap();

    printf("bone_overlay_tests: %d passed, %d failed\n",
           g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
