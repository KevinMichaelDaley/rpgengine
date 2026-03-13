/**
 * @file p202_viewport_camera_tests.c
 * @brief Tests for the editor viewport camera system.
 *
 * Covers: init defaults, orbit, pan, zoom, snap views,
 * view/projection matrix computation, eye position, screen-to-ray,
 * pitch/zoom clamping, and edge cases.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/math/constants.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _d = (float)(exp) - (float)(act);                                \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tol)) {                                                     \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "              \
                    "expected %.6f got %.6f (tol %.6f)\n",                    \
                    __FILE__, __LINE__, (double)(exp), (double)(act),          \
                    (double)(tol));                                            \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

static int test_camera_init_defaults(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Default focus at origin. */
    ASSERT_FLOAT_NEAR(0.0f, cam.focus.x, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.focus.y, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.focus.z, 0.0001f);

    /* Default yaw/pitch. */
    ASSERT_FLOAT_NEAR(0.0f, cam.yaw, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.pitch, 0.0001f);

    /* Default distance. */
    ASSERT_FLOAT_NEAR(10.0f, cam.distance, 0.0001f);

    /* Perspective by default. */
    ASSERT_TRUE(!cam.orthographic);

    /* Sane FOV. */
    ASSERT_TRUE(cam.fov > 0.1f && cam.fov < 3.2f);

    /* Sane near/far. */
    ASSERT_TRUE(cam.near_plane > 0.0f);
    ASSERT_TRUE(cam.far_plane > cam.near_plane);

    return 0;
}

static int test_camera_orbit(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_camera_orbit(&cam, 0.5f, 0.3f);
    ASSERT_FLOAT_NEAR(0.5f, cam.yaw, 0.0001f);
    ASSERT_FLOAT_NEAR(0.3f, cam.pitch, 0.0001f);

    /* Accumulates. */
    editor_camera_orbit(&cam, 0.2f, 0.1f);
    ASSERT_FLOAT_NEAR(0.7f, cam.yaw, 0.0001f);
    ASSERT_FLOAT_NEAR(0.4f, cam.pitch, 0.0001f);

    return 0;
}

static int test_camera_orbit_pitch_clamp(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Pitch should be clamped to [-89, +89] degrees (in radians). */
    float max_pitch = 89.0f * FERRUM_PI / 180.0f;

    editor_camera_orbit(&cam, 0.0f, 100.0f);
    ASSERT_TRUE(cam.pitch <= max_pitch + 0.001f);

    editor_camera_init(&cam);
    editor_camera_orbit(&cam, 0.0f, -100.0f);
    ASSERT_TRUE(cam.pitch >= -max_pitch - 0.001f);

    return 0;
}

static int test_camera_pan(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Pan by screen-space delta. Camera at default (yaw=0, pitch=0)
     * looks along -Z, so right = +X, up = +Y. */
    editor_camera_pan(&cam, 2.0f, 3.0f);

    /* Focus should have moved in camera-local right and up. */
    /* At yaw=0, right = +X, up = +Y. */
    ASSERT_TRUE(fabsf(cam.focus.x - 2.0f) < 0.1f ||
                fabsf(cam.focus.x + 2.0f) < 0.1f);
    /* Y should have changed. */
    ASSERT_TRUE(fabsf(cam.focus.y) > 0.5f);

    return 0;
}

static int test_camera_zoom(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    float orig = cam.distance;

    editor_camera_zoom(&cam, -2.0f);
    ASSERT_FLOAT_NEAR(orig - 2.0f, cam.distance, 0.01f);

    editor_camera_zoom(&cam, 1.0f);
    ASSERT_FLOAT_NEAR(orig - 1.0f, cam.distance, 0.01f);

    return 0;
}

static int test_camera_zoom_clamp_min(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Zoom all the way in — should not go below minimum. */
    editor_camera_zoom(&cam, -1000.0f);
    ASSERT_TRUE(cam.distance >= EDITOR_CAMERA_MIN_DISTANCE - 0.001f);

    return 0;
}

static int test_camera_zoom_clamp_max(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_camera_zoom(&cam, 100000.0f);
    ASSERT_TRUE(cam.distance <= EDITOR_CAMERA_MAX_DISTANCE + 0.001f);

    return 0;
}

static int test_camera_snap_front(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    editor_camera_orbit(&cam, 1.0f, 0.5f);

    editor_camera_snap_view(&cam, EDITOR_VIEW_FRONT);
    /* Front: looking along +Z, so yaw = 0, pitch = 0. */
    ASSERT_FLOAT_NEAR(0.0f, cam.yaw, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.pitch, 0.001f);

    return 0;
}

static int test_camera_snap_right(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_camera_snap_view(&cam, EDITOR_VIEW_RIGHT);
    /* Right: looking along -X, yaw = PI/2. */
    ASSERT_FLOAT_NEAR(FERRUM_PI_2, cam.yaw, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.pitch, 0.001f);

    return 0;
}

static int test_camera_snap_top(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    editor_camera_snap_view(&cam, EDITOR_VIEW_TOP);
    /* Top: looking down, pitch = -PI/2. */
    float expected_pitch = -FERRUM_PI_2;
    ASSERT_FLOAT_NEAR(0.0f, cam.yaw, 0.001f);
    ASSERT_FLOAT_NEAR(expected_pitch, cam.pitch, 0.01f);

    return 0;
}

static int test_camera_toggle_projection(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    ASSERT_TRUE(!cam.orthographic);

    editor_camera_toggle_projection(&cam);
    ASSERT_TRUE(cam.orthographic);

    editor_camera_toggle_projection(&cam);
    ASSERT_TRUE(!cam.orthographic);

    return 0;
}

static int test_camera_eye_position(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    /* Default: focus=(0,0,0), yaw=0, pitch=0, distance=10.
     * Camera looks along -Z, so eye should be at (0, 0, 10). */
    vec3_t eye = editor_camera_eye_position(&cam);
    ASSERT_FLOAT_NEAR(0.0f, eye.x, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, eye.y, 0.01f);
    ASSERT_FLOAT_NEAR(10.0f, eye.z, 0.01f);

    return 0;
}

static int test_camera_eye_after_orbit(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    /* Orbit to yaw = PI/2 (90 degrees). Eye should move to (+10, 0, 0). */
    editor_camera_orbit(&cam, FERRUM_PI_2, 0.0f);
    vec3_t eye = editor_camera_eye_position(&cam);
    ASSERT_FLOAT_NEAR(10.0f, eye.x, 0.1f);
    ASSERT_FLOAT_NEAR(0.0f, eye.y, 0.1f);
    ASSERT_FLOAT_NEAR(0.0f, eye.z, 0.5f);

    return 0;
}

static int test_camera_view_matrix(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    mat4_t view;
    int rc = editor_camera_view_matrix(&cam, &view);
    ASSERT_TRUE(rc == 0);

    /* View matrix should be valid — multiply eye by it and check. */
    /* The view matrix transforms world→camera coords.
     * With default camera at (0,0,10) looking at origin,
     * origin in camera space should be at (0, 0, -10). */
    vec4_t origin = {0.0f, 0.0f, 0.0f, 1.0f};
    vec4_t result = mat4_mul_vec4(view, origin);
    ASSERT_FLOAT_NEAR(0.0f, result.x, 0.1f);
    ASSERT_FLOAT_NEAR(0.0f, result.y, 0.1f);
    ASSERT_FLOAT_NEAR(-10.0f, result.z, 0.1f);

    return 0;
}

static int test_camera_perspective_matrix(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    mat4_t proj;
    int rc = editor_camera_projection_matrix(&cam, 16.0f / 9.0f, &proj);
    ASSERT_TRUE(rc == 0);

    /* Perspective matrix should have non-zero diagonal elements. */
    ASSERT_TRUE(fabsf(proj.m[0]) > 0.01f);
    ASSERT_TRUE(fabsf(proj.m[5]) > 0.01f);
    ASSERT_TRUE(fabsf(proj.m[10]) > 0.01f);

    return 0;
}

static int test_camera_ortho_matrix(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);
    cam.orthographic = true;

    mat4_t proj;
    int rc = editor_camera_projection_matrix(&cam, 1.0f, &proj);
    ASSERT_TRUE(rc == 0);

    /* Ortho: m[15] should be 1.0 (not a perspective divide). */
    ASSERT_FLOAT_NEAR(1.0f, proj.m[15], 0.001f);
    /* Ortho: m[11] should be 0 (no perspective w). */
    ASSERT_FLOAT_NEAR(0.0f, proj.m[11], 0.001f);

    return 0;
}

static int test_camera_screen_to_ray(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Cast ray from center of screen. */
    vec2_t screen_center = {0.5f, 0.5f};
    vec2_t viewport_size = {800.0f, 600.0f};

    editor_ray_t ray;
    int rc = editor_camera_screen_to_ray(&cam, screen_center, viewport_size,
                                          &ray);
    ASSERT_TRUE(rc == 0);

    /* Origin should be at eye position. */
    vec3_t eye = editor_camera_eye_position(&cam);
    ASSERT_FLOAT_NEAR(eye.x, ray.origin.x, 0.01f);
    ASSERT_FLOAT_NEAR(eye.y, ray.origin.y, 0.01f);
    ASSERT_FLOAT_NEAR(eye.z, ray.origin.z, 0.01f);

    /* Direction from center should point toward focus (along -Z). */
    ASSERT_TRUE(ray.direction.z < -0.5f);

    return 0;
}

static int test_camera_screen_to_ray_corner(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Cast from top-left corner. */
    vec2_t corner = {0.0f, 0.0f};
    vec2_t viewport_size = {800.0f, 600.0f};

    editor_ray_t ray;
    int rc = editor_camera_screen_to_ray(&cam, corner, viewport_size, &ray);
    ASSERT_TRUE(rc == 0);

    /* Direction should still be roughly forward but offset. */
    ASSERT_TRUE(ray.direction.z < 0.0f);

    /* Ray direction should be normalized. */
    float len = vec3_magnitude(ray.direction);
    ASSERT_FLOAT_NEAR(1.0f, len, 0.01f);

    return 0;
}

static int test_camera_frame_selection(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Frame a bounding box centered at (5, 3, -2) with size (4, 2, 6). */
    vec3_t aabb_min = {3.0f, 2.0f, -5.0f};
    vec3_t aabb_max = {7.0f, 4.0f, 1.0f};

    editor_camera_frame_selection(&cam, aabb_min, aabb_max);

    /* Focus should be at AABB center. */
    ASSERT_FLOAT_NEAR(5.0f, cam.focus.x, 0.1f);
    ASSERT_FLOAT_NEAR(3.0f, cam.focus.y, 0.1f);
    ASSERT_FLOAT_NEAR(-2.0f, cam.focus.z, 0.1f);

    /* Distance should be enough to see the object. */
    ASSERT_TRUE(cam.distance > 1.0f);

    return 0;
}

static int test_camera_reset(void) {
    editor_camera_t cam;
    editor_camera_init(&cam);

    /* Modify everything. */
    editor_camera_orbit(&cam, 1.5f, 0.8f);
    editor_camera_pan(&cam, 5.0f, 3.0f);
    editor_camera_zoom(&cam, 5.0f);

    /* Reset to defaults. */
    editor_camera_reset(&cam);

    ASSERT_FLOAT_NEAR(0.0f, cam.focus.x, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.focus.y, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.focus.z, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.yaw, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, cam.pitch, 0.0001f);
    ASSERT_FLOAT_NEAR(10.0f, cam.distance, 0.0001f);

    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"camera_init_defaults",       test_camera_init_defaults},
    {"camera_orbit",               test_camera_orbit},
    {"camera_orbit_pitch_clamp",   test_camera_orbit_pitch_clamp},
    {"camera_pan",                 test_camera_pan},
    {"camera_zoom",                test_camera_zoom},
    {"camera_zoom_clamp_min",      test_camera_zoom_clamp_min},
    {"camera_zoom_clamp_max",      test_camera_zoom_clamp_max},
    {"camera_snap_front",          test_camera_snap_front},
    {"camera_snap_right",          test_camera_snap_right},
    {"camera_snap_top",            test_camera_snap_top},
    {"camera_toggle_projection",   test_camera_toggle_projection},
    {"camera_eye_position",        test_camera_eye_position},
    {"camera_eye_after_orbit",     test_camera_eye_after_orbit},
    {"camera_view_matrix",         test_camera_view_matrix},
    {"camera_perspective_matrix",  test_camera_perspective_matrix},
    {"camera_ortho_matrix",        test_camera_ortho_matrix},
    {"camera_screen_to_ray",       test_camera_screen_to_ray},
    {"camera_screen_to_ray_corner", test_camera_screen_to_ray_corner},
    {"camera_frame_selection",     test_camera_frame_selection},
    {"camera_reset",               test_camera_reset},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
