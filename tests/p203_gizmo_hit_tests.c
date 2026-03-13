/**
 * @file p203_gizmo_hit_tests.c
 * @brief Tests for transform gizmo hit testing — axis arrows, rotation
 *        rings, scale cubes.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/viewport/viewport_gizmo.h"
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

/* ---- Init tests ---- */

static int test_gizmo_init(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);

    ASSERT_TRUE(gizmo.mode == GIZMO_MODE_TRANSLATE);
    ASSERT_TRUE(gizmo.active_axis == GIZMO_AXIS_NONE);
    ASSERT_TRUE(!gizmo.dragging);
    ASSERT_FLOAT_NEAR(0.0f, gizmo.position.x, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, gizmo.position.y, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, gizmo.position.z, 0.0001f);

    return 0;
}

static int test_gizmo_set_mode(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);

    gizmo_state_set_mode(&gizmo, GIZMO_MODE_ROTATE);
    ASSERT_TRUE(gizmo.mode == GIZMO_MODE_ROTATE);

    gizmo_state_set_mode(&gizmo, GIZMO_MODE_SCALE);
    ASSERT_TRUE(gizmo.mode == GIZMO_MODE_SCALE);

    return 0;
}

/* ---- Translate gizmo hit tests ---- */

static int test_gizmo_hit_x_axis(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);
    gizmo.mode = GIZMO_MODE_TRANSLATE;
    gizmo.position = (vec3_t){0.0f, 0.0f, 0.0f};

    /* Ray along X axis, close to the gizmo X arrow. */
    editor_ray_t ray = {
        .origin = {0.5f, 0.0f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };

    gizmo_axis_t hit = gizmo_hit_test(&gizmo, &ray, 1.0f);
    ASSERT_TRUE(hit == GIZMO_AXIS_X);

    return 0;
}

static int test_gizmo_hit_y_axis(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);
    gizmo.mode = GIZMO_MODE_TRANSLATE;

    /* Ray near the Y arrow. */
    editor_ray_t ray = {
        .origin = {0.0f, 0.5f, 5.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };

    gizmo_axis_t hit = gizmo_hit_test(&gizmo, &ray, 1.0f);
    ASSERT_TRUE(hit == GIZMO_AXIS_Y);

    return 0;
}

static int test_gizmo_hit_z_axis(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);
    gizmo.mode = GIZMO_MODE_TRANSLATE;

    /* Ray near the Z arrow — approaching from the side. */
    editor_ray_t ray = {
        .origin = {5.0f, 0.0f, 0.5f},
        .direction = {-1.0f, 0.0f, 0.0f}
    };

    gizmo_axis_t hit = gizmo_hit_test(&gizmo, &ray, 1.0f);
    ASSERT_TRUE(hit == GIZMO_AXIS_Z);

    return 0;
}

static int test_gizmo_hit_miss(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);

    /* Ray far away from gizmo. */
    editor_ray_t ray = {
        .origin = {100.0f, 100.0f, 100.0f},
        .direction = {0.0f, 0.0f, -1.0f}
    };

    gizmo_axis_t hit = gizmo_hit_test(&gizmo, &ray, 1.0f);
    ASSERT_TRUE(hit == GIZMO_AXIS_NONE);

    return 0;
}

/* ---- Drag computation ---- */

static int test_gizmo_drag_translate(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);
    gizmo.mode = GIZMO_MODE_TRANSLATE;
    gizmo.active_axis = GIZMO_AXIS_X;
    gizmo.dragging = true;
    gizmo.position = (vec3_t){0.0f, 0.0f, 0.0f};

    /* Compute delta from drag. */
    vec3_t drag_start = {1.0f, 0.0f, 0.0f};
    vec3_t drag_current = {3.0f, 0.5f, 0.0f};

    vec3_t delta = gizmo_compute_drag_delta(&gizmo, drag_start, drag_current);

    /* Only X should change (constrained to X axis). */
    ASSERT_FLOAT_NEAR(2.0f, delta.x, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, delta.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, delta.z, 0.01f);

    return 0;
}

static int test_gizmo_drag_translate_y(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);
    gizmo.mode = GIZMO_MODE_TRANSLATE;
    gizmo.active_axis = GIZMO_AXIS_Y;
    gizmo.dragging = true;

    vec3_t drag_start = {0.0f, 1.0f, 0.0f};
    vec3_t drag_current = {0.5f, 4.0f, 0.0f};

    vec3_t delta = gizmo_compute_drag_delta(&gizmo, drag_start, drag_current);

    ASSERT_FLOAT_NEAR(0.0f, delta.x, 0.01f);
    ASSERT_FLOAT_NEAR(3.0f, delta.y, 0.01f);
    ASSERT_FLOAT_NEAR(0.0f, delta.z, 0.01f);

    return 0;
}

static int test_gizmo_drag_no_axis(void) {
    gizmo_state_t gizmo;
    gizmo_state_init(&gizmo);
    gizmo.active_axis = GIZMO_AXIS_NONE;

    vec3_t drag_start = {0.0f, 0.0f, 0.0f};
    vec3_t drag_current = {5.0f, 5.0f, 5.0f};

    vec3_t delta = gizmo_compute_drag_delta(&gizmo, drag_start, drag_current);

    ASSERT_FLOAT_NEAR(0.0f, delta.x, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, delta.y, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, delta.z, 0.0001f);

    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"gizmo_init",              test_gizmo_init},
    {"gizmo_set_mode",          test_gizmo_set_mode},
    {"gizmo_hit_x_axis",        test_gizmo_hit_x_axis},
    {"gizmo_hit_y_axis",        test_gizmo_hit_y_axis},
    {"gizmo_hit_z_axis",        test_gizmo_hit_z_axis},
    {"gizmo_hit_miss",          test_gizmo_hit_miss},
    {"gizmo_drag_translate",    test_gizmo_drag_translate},
    {"gizmo_drag_translate_y",  test_gizmo_drag_translate_y},
    {"gizmo_drag_no_axis",      test_gizmo_drag_no_axis},
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
