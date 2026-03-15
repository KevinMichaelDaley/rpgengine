/**
 * @file p206a_snap_state_tests.c
 * @brief Unit tests for snap state: grid quantization, per-axis toggles.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "ferrum/editor/scene/snap_state.h"

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                        \
        float _e = (float)(exp);                                               \
        float _a = (float)(act);                                               \
        if (fabsf(_e - _a) > (eps)) {                                          \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f "   \
                    "got %f\n", __FILE__, __LINE__, (double)_e, (double)_a);   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Happy path ---- */

static int test_snap_disabled_passthrough(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    /* Snap disabled by default — value passes through unchanged */
    float result = snap_state_quantize(&snap, SNAP_POSITION, 1.7f, 0);
    ASSERT_FLOAT_NEAR(1.7f, result, 0.001f);
    return 0;
}

static int test_snap_enabled_rounds(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 1.0f;

    ASSERT_FLOAT_NEAR(2.0f, snap_state_quantize(&snap, SNAP_POSITION, 1.7f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, snap_state_quantize(&snap, SNAP_POSITION, 1.3f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, snap_state_quantize(&snap, SNAP_POSITION, 1.5f, 0), 0.001f);
    return 0;
}

static int test_snap_rotation_grid(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_ROTATION] = true;
    /* Default rotation grid is 15 degrees */
    ASSERT_FLOAT_NEAR(15.0f, snap_state_quantize(&snap, SNAP_ROTATION, 12.0f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(30.0f, snap_state_quantize(&snap, SNAP_ROTATION, 23.0f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f,  snap_state_quantize(&snap, SNAP_ROTATION, 6.0f, 0), 0.001f);
    return 0;
}

static int test_snap_scale_grid(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_SCALE] = true;
    /* Default scale grid is 1.0 */
    ASSERT_FLOAT_NEAR(1.0f, snap_state_quantize(&snap, SNAP_SCALE, 1.47f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, snap_state_quantize(&snap, SNAP_SCALE, 1.53f, 0), 0.001f);
    return 0;
}

/* ---- Per-axis toggles ---- */

static int test_snap_per_axis_disabled(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.axis_y[SNAP_POSITION] = false; /* disable Y axis snap */

    /* X snaps */
    ASSERT_FLOAT_NEAR(2.0f, snap_state_quantize(&snap, SNAP_POSITION, 1.7f, 0), 0.001f);
    /* Y passes through */
    ASSERT_FLOAT_NEAR(1.7f, snap_state_quantize(&snap, SNAP_POSITION, 1.7f, 1), 0.001f);
    /* Z snaps */
    ASSERT_FLOAT_NEAR(2.0f, snap_state_quantize(&snap, SNAP_POSITION, 1.7f, 2), 0.001f);
    return 0;
}

/* ---- Edge cases ---- */

static int test_snap_negative_coords(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 1.0f;

    ASSERT_FLOAT_NEAR(-2.0f, snap_state_quantize(&snap, SNAP_POSITION, -1.7f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(-1.0f, snap_state_quantize(&snap, SNAP_POSITION, -1.3f, 0), 0.001f);
    return 0;
}

static int test_snap_near_zero(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 1.0f;

    ASSERT_FLOAT_NEAR(0.0f, snap_state_quantize(&snap, SNAP_POSITION, 0.1f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, snap_state_quantize(&snap, SNAP_POSITION, -0.1f, 0), 0.001f);
    return 0;
}

static int test_snap_custom_grid(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 0.25f;

    ASSERT_FLOAT_NEAR(1.75f, snap_state_quantize(&snap, SNAP_POSITION, 1.7f, 0), 0.001f);
    ASSERT_FLOAT_NEAR(1.5f,  snap_state_quantize(&snap, SNAP_POSITION, 1.6f, 0), 0.001f);
    return 0;
}

static int test_snap_invalid_type(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    /* Out of range type should passthrough */
    float result = snap_state_quantize(&snap, (snap_transform_type_t)99, 1.7f, 0);
    ASSERT_FLOAT_NEAR(1.7f, result, 0.001f);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"snap_disabled_passthrough",  test_snap_disabled_passthrough},
    {"snap_enabled_rounds",        test_snap_enabled_rounds},
    {"snap_rotation_grid",         test_snap_rotation_grid},
    {"snap_scale_grid",            test_snap_scale_grid},
    {"snap_per_axis_disabled",     test_snap_per_axis_disabled},
    {"snap_negative_coords",       test_snap_negative_coords},
    {"snap_near_zero",             test_snap_near_zero},
    {"snap_custom_grid",           test_snap_custom_grid},
    {"snap_invalid_type",          test_snap_invalid_type},
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
