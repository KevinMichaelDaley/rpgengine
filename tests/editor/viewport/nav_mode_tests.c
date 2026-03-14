/**
 * @file nav_mode_tests.c
 * @brief Tests for per-viewport navigation mode system.
 *
 * Tests cover nav mode cycling, fly camera movement, and
 * mode-specific camera behavior.
 */

#include "ferrum/editor/viewport/viewport_nav.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/vec3.h"

#define _USE_MATH_DEFINES
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ---- Minimal test harness ---- */

static int s_pass, s_fail;

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ASSERT_EQ FAILED: %s == %d, expected %d (line %d)\n", \
               #a, (int)(a), (int)(b), __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_STREQ(a, b) do { \
    if (strcmp((a), (b)) != 0) { \
        printf("  ASSERT_STREQ FAILED: \"%s\" != \"%s\" (line %d)\n", \
               (a), (b), __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_NEAR(a, b, eps) do { \
    if (fabsf((a) - (b)) > (eps)) { \
        printf("  ASSERT_NEAR FAILED: %s == %f, expected %f (line %d)\n", \
               #a, (double)(a), (double)(b), __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_TRUE(x) do { \
    if (!(x)) { \
        printf("  ASSERT_TRUE FAILED: %s (line %d)\n", #x, __LINE__); \
        return false; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("RUN  " #fn "\n"); \
    if (fn()) { s_pass++; printf("OK   " #fn "\n"); } \
    else { s_fail++; printf("FAIL " #fn "\n"); } \
} while (0)

/* ---- Tests: nav mode enum ---- */

/** Nav mode constants have expected values. */
static bool test_nav_mode_values(void) {
    ASSERT_EQ(NAV_MODE_ORBIT_SELECTION, 0);
    ASSERT_EQ(NAV_MODE_ORBIT_CURSOR, 1);
    ASSERT_EQ(NAV_MODE_FLY, 2);
    ASSERT_EQ(NAV_MODE_PAN_ZOOM, 3);
    ASSERT_EQ(NAV_MODE_COUNT, 4);
    return true;
}

/** Nav mode names return correct strings. */
static bool test_nav_mode_names(void) {
    ASSERT_STREQ(nav_mode_name(NAV_MODE_ORBIT_SELECTION), "Orbit");
    ASSERT_STREQ(nav_mode_name(NAV_MODE_ORBIT_CURSOR), "OrbCur");
    ASSERT_STREQ(nav_mode_name(NAV_MODE_FLY), "Fly");
    ASSERT_STREQ(nav_mode_name(NAV_MODE_PAN_ZOOM), "PanZm");
    return true;
}

/** Nav mode cycling wraps around. */
static bool test_nav_mode_cycle(void) {
    ASSERT_EQ(nav_mode_next(NAV_MODE_ORBIT_SELECTION), NAV_MODE_ORBIT_CURSOR);
    ASSERT_EQ(nav_mode_next(NAV_MODE_ORBIT_CURSOR), NAV_MODE_FLY);
    ASSERT_EQ(nav_mode_next(NAV_MODE_FLY), NAV_MODE_PAN_ZOOM);
    ASSERT_EQ(nav_mode_next(NAV_MODE_PAN_ZOOM), NAV_MODE_ORBIT_SELECTION);
    return true;
}

/** Out-of-range mode wraps to first. */
static bool test_nav_mode_cycle_invalid(void) {
    /* (99 + 1) % 4 = 0 → wraps to ORBIT_SELECTION. */
    ASSERT_EQ(nav_mode_next(99), NAV_MODE_ORBIT_SELECTION);
    return true;
}

/* ---- Tests: fly camera movement ---- */

/** Fly forward moves along look direction. */
static bool test_fly_forward(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    /* Default: yaw=0, pitch=0 → looking along +Z. */
    /* Set camera to fly position. */
    cam.focus = (vec3_t){0, 0, 0};
    cam.distance = 0.0f;

    editor_camera_fly_move(&cam, 1.0f, 0.0f, 0.0f);

    /* Forward with yaw=0 pitch=0 should move along +Z
     * (forward = (sin(yaw)*cos(pitch), sin(pitch), cos(yaw)*cos(pitch))
     *          = (0, 0, 1)) */
    ASSERT_NEAR(cam.focus.x, 0.0f, 0.01f);
    ASSERT_NEAR(cam.focus.y, 0.0f, 0.01f);
    ASSERT_NEAR(cam.focus.z, 1.0f, 0.01f);
    return true;
}

/** Fly strafe right moves perpendicular to look direction. */
static bool test_fly_strafe(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.focus = (vec3_t){0, 0, 0};
    cam.distance = 0.0f;

    editor_camera_fly_move(&cam, 0.0f, 1.0f, 0.0f);

    /* Right with yaw=0: right = (cos(yaw), 0, -sin(yaw)) = (1, 0, 0) */
    ASSERT_NEAR(cam.focus.x, 1.0f, 0.01f);
    ASSERT_NEAR(cam.focus.y, 0.0f, 0.01f);
    ASSERT_NEAR(cam.focus.z, 0.0f, 0.01f);
    return true;
}

/** Fly up moves along world Y. */
static bool test_fly_up(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.focus = (vec3_t){0, 0, 0};
    cam.distance = 0.0f;

    editor_camera_fly_move(&cam, 0.0f, 0.0f, 1.0f);

    ASSERT_NEAR(cam.focus.x, 0.0f, 0.01f);
    ASSERT_NEAR(cam.focus.y, 1.0f, 0.01f);
    ASSERT_NEAR(cam.focus.z, 0.0f, 0.01f);
    return true;
}

/** Fly forward with rotated yaw moves correctly. */
static bool test_fly_rotated(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.focus = (vec3_t){0, 0, 0};
    cam.distance = 0.0f;
    cam.yaw = (float)(M_PI / 2.0); /* 90° → looking along +X */

    editor_camera_fly_move(&cam, 2.0f, 0.0f, 0.0f);

    /* Forward with yaw=π/2: forward = (sin(π/2), 0, cos(π/2)) = (1, 0, 0) */
    ASSERT_NEAR(cam.focus.x, 2.0f, 0.01f);
    ASSERT_NEAR(cam.focus.y, 0.0f, 0.01f);
    ASSERT_NEAR(cam.focus.z, 0.0f, 0.1f);
    return true;
}

/** Fly backward moves in negative forward direction. */
static bool test_fly_backward(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.focus = (vec3_t){5, 0, 5};
    cam.distance = 0.0f;

    editor_camera_fly_move(&cam, -1.0f, 0.0f, 0.0f);

    /* Backward with yaw=0: -forward = (0, 0, -1) */
    ASSERT_NEAR(cam.focus.x, 5.0f, 0.01f);
    ASSERT_NEAR(cam.focus.y, 0.0f, 0.01f);
    ASSERT_NEAR(cam.focus.z, 4.0f, 0.01f);
    return true;
}

/** Switching to fly mode: eye becomes focus, distance zeroed. */
static bool test_fly_enter(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    /* Default orbit: focus=(0,0,0), distance=10, yaw=0, pitch=0.
     * Eye = focus + (sin(0)*cos(0), sin(0), cos(0)*cos(0)) * 10
     *     = (0, 0, 10). */
    vec3_t eye = editor_camera_eye_position(&cam);

    editor_camera_enter_fly(&cam);

    ASSERT_NEAR(cam.focus.x, eye.x, 0.01f);
    ASSERT_NEAR(cam.focus.y, eye.y, 0.01f);
    ASSERT_NEAR(cam.focus.z, eye.z, 0.01f);
    ASSERT_NEAR(cam.distance, 0.0f, 0.01f);
    return true;
}

/** Switching from fly to orbit: focus placed at look-ahead distance. */
static bool test_fly_exit(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.focus = (vec3_t){10, 5, 20};
    cam.distance = 0.0f;
    cam.yaw = 0.0f;
    cam.pitch = 0.0f;

    editor_camera_exit_fly(&cam, 8.0f);

    /* Focus should be at position + forward * distance.
     * Forward = (0, 0, 1), so focus = (10, 5, 28). */
    ASSERT_NEAR(cam.focus.x, 10.0f, 0.01f);
    ASSERT_NEAR(cam.focus.y, 5.0f, 0.01f);
    ASSERT_NEAR(cam.focus.z, 28.0f, 0.01f);
    ASSERT_NEAR(cam.distance, 8.0f, 0.01f);
    return true;
}

/** Fly mode: eye position equals focus when distance is 0. */
static bool test_fly_eye_at_focus(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.focus = (vec3_t){3, 7, 11};
    cam.distance = 0.0f;

    vec3_t eye = editor_camera_eye_position(&cam);
    /* With distance=0, eye should be at focus. */
    ASSERT_NEAR(eye.x, 3.0f, 0.01f);
    ASSERT_NEAR(eye.y, 7.0f, 0.01f);
    ASSERT_NEAR(eye.z, 11.0f, 0.01f);
    return true;
}

/* ---- Tests: nav mode queries ---- */

/** nav_mode_allows_orbit returns correct values. */
static bool test_allows_orbit(void) {
    ASSERT_TRUE(nav_mode_allows_orbit(NAV_MODE_ORBIT_SELECTION));
    ASSERT_TRUE(nav_mode_allows_orbit(NAV_MODE_ORBIT_CURSOR));
    ASSERT_TRUE(!nav_mode_allows_orbit(NAV_MODE_FLY));
    ASSERT_TRUE(!nav_mode_allows_orbit(NAV_MODE_PAN_ZOOM));
    return true;
}

/** nav_mode_allows_fly returns correct values. */
static bool test_allows_fly(void) {
    ASSERT_TRUE(!nav_mode_allows_fly(NAV_MODE_ORBIT_SELECTION));
    ASSERT_TRUE(!nav_mode_allows_fly(NAV_MODE_ORBIT_CURSOR));
    ASSERT_TRUE(nav_mode_allows_fly(NAV_MODE_FLY));
    ASSERT_TRUE(!nav_mode_allows_fly(NAV_MODE_PAN_ZOOM));
    return true;
}

/* ---- Main ---- */

int main(void) {
    RUN(test_nav_mode_values);
    RUN(test_nav_mode_names);
    RUN(test_nav_mode_cycle);
    RUN(test_nav_mode_cycle_invalid);
    RUN(test_fly_forward);
    RUN(test_fly_strafe);
    RUN(test_fly_up);
    RUN(test_fly_rotated);
    RUN(test_fly_backward);
    RUN(test_fly_enter);
    RUN(test_fly_exit);
    RUN(test_fly_eye_at_focus);
    RUN(test_allows_orbit);
    RUN(test_allows_fly);

    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
