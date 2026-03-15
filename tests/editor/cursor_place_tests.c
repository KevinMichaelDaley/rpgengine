/**
 * @file cursor_place_tests.c
 * @brief Tests for ray-plane intersection used by Ctrl+right-click cursor placement.
 *
 * Tests cover:
 *   - Ray pointing down hits Y=0 plane at expected point
 *   - Horizontal (parallel) ray returns no hit
 *   - Ray pointing away from plane returns no hit
 *   - Ray hits an offset plane (Y=5)
 */

#include <math.h>
#include <stdio.h>
#include <stdbool.h>

#include "ferrum/math/vec3.h"
#include "ferrum/editor/scene/cursor_place.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                              */
/* ----------------------------------------------------------------------- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) \
    ASSERT(fabsf((a) - (b)) < (eps))

/* ----------------------------------------------------------------------- */
/* Tests                                                                    */
/* ----------------------------------------------------------------------- */

/**
 * Ray from (0,10,0) pointing straight down should hit Y=0 plane
 * at (0,0,0) with t=10.
 */
static bool test_ray_plane_hit(void) {
    vec3_t origin    = {0.0f, 10.0f, 0.0f};
    vec3_t direction = {0.0f, -1.0f, 0.0f};
    float plane_y = 0.0f;
    float t_out = 0.0f;
    vec3_t hit_point;

    bool hit = cursor_ray_plane_intersect(origin, direction,
                                           plane_y, &t_out, &hit_point);
    ASSERT(hit);
    ASSERT_NEAR(t_out, 10.0f, 1e-4f);
    ASSERT_NEAR(hit_point.x, 0.0f, 1e-4f);
    ASSERT_NEAR(hit_point.y, 0.0f, 1e-4f);
    ASSERT_NEAR(hit_point.z, 0.0f, 1e-4f);
    return true;
}

/**
 * Ray from (5,10,3) pointing at an angle should hit Y=0 plane
 * at the correct XZ offset.  Direction = (1, -2, 0) normalized.
 */
static bool test_ray_plane_hit_angled(void) {
    float len = sqrtf(1.0f + 4.0f); /* sqrt(5) */
    vec3_t origin    = {5.0f, 10.0f, 3.0f};
    vec3_t direction = {1.0f / len, -2.0f / len, 0.0f};
    float plane_y = 0.0f;
    float t_out = 0.0f;
    vec3_t hit_point;

    bool hit = cursor_ray_plane_intersect(origin, direction,
                                           plane_y, &t_out, &hit_point);
    ASSERT(hit);
    /* t = (0 - 10) / (-2/sqrt5) = 10*sqrt5/2 = 5*sqrt5 */
    float expected_t = 5.0f * len;
    ASSERT_NEAR(t_out, expected_t, 1e-3f);
    /* hit x = 5 + t*(1/sqrt5) = 5 + 5*sqrt5*(1/sqrt5) = 5 + 5 = 10 */
    ASSERT_NEAR(hit_point.x, 10.0f, 1e-3f);
    ASSERT_NEAR(hit_point.y, 0.0f, 1e-4f);
    ASSERT_NEAR(hit_point.z, 3.0f, 1e-3f);
    return true;
}

/**
 * Horizontal ray (direction.y == 0) is parallel to Y=0 plane.
 * Should return no hit.
 */
static bool test_ray_plane_parallel(void) {
    vec3_t origin    = {0.0f, 5.0f, 0.0f};
    vec3_t direction = {1.0f, 0.0f, 0.0f};
    float plane_y = 0.0f;
    float t_out = 0.0f;
    vec3_t hit_point;

    bool hit = cursor_ray_plane_intersect(origin, direction,
                                           plane_y, &t_out, &hit_point);
    ASSERT(!hit);
    return true;
}

/**
 * Ray pointing away from the plane (upward from above Y=0).
 * Should return no hit (t would be negative).
 */
static bool test_ray_plane_behind(void) {
    vec3_t origin    = {0.0f, 5.0f, 0.0f};
    vec3_t direction = {0.0f, 1.0f, 0.0f}; /* pointing up, away from Y=0 */
    float plane_y = 0.0f;
    float t_out = 0.0f;
    vec3_t hit_point;

    bool hit = cursor_ray_plane_intersect(origin, direction,
                                           plane_y, &t_out, &hit_point);
    ASSERT(!hit);
    return true;
}

/**
 * Ray from (0,10,0) pointing down hits Y=5 offset plane.
 * Expected hit at (0,5,0) with t=5.
 */
static bool test_ray_plane_offset(void) {
    vec3_t origin    = {0.0f, 10.0f, 0.0f};
    vec3_t direction = {0.0f, -1.0f, 0.0f};
    float plane_y = 5.0f;
    float t_out = 0.0f;
    vec3_t hit_point;

    bool hit = cursor_ray_plane_intersect(origin, direction,
                                           plane_y, &t_out, &hit_point);
    ASSERT(hit);
    ASSERT_NEAR(t_out, 5.0f, 1e-4f);
    ASSERT_NEAR(hit_point.x, 0.0f, 1e-4f);
    ASSERT_NEAR(hit_point.y, 5.0f, 1e-4f);
    ASSERT_NEAR(hit_point.z, 0.0f, 1e-4f);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                     */
/* ----------------------------------------------------------------------- */

int main(void) {
    g_pass = 0;
    g_fail = 0;

    RUN(test_ray_plane_hit);
    RUN(test_ray_plane_hit_angled);
    RUN(test_ray_plane_parallel);
    RUN(test_ray_plane_behind);
    RUN(test_ray_plane_offset);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
