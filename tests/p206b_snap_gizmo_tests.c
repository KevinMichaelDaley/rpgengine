/**
 * @file p206b_snap_gizmo_tests.c
 * @brief Tests for snap_apply_position, snap_apply_rotation, snap_apply_scale.
 *
 * These helpers snap absolute values to the grid and compute the corrected
 * delta for the gizmo pipeline.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>

#include "ferrum/editor/scene/snap_state.h"
#include "ferrum/math/vec3.h"

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

/* ---- snap_apply_position: snaps absolute target, returns corrected delta -- */

static int test_snap_position_disabled(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    /* Snap disabled: delta passes through unchanged. */
    vec3_t origin = {1.0f, 2.0f, 3.0f};
    vec3_t accum  = {0.3f, 0.7f, 0.1f};
    vec3_t result = snap_apply_position(&snap, origin, accum);
    ASSERT_FLOAT_NEAR(0.3f, result.x, 0.001f);
    ASSERT_FLOAT_NEAR(0.7f, result.y, 0.001f);
    ASSERT_FLOAT_NEAR(0.1f, result.z, 0.001f);
    return 0;
}

static int test_snap_position_rounds_target(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 1.0f;
    /* origin = (1.0, 2.0, 3.0), accum = (0.3, 0.7, 0.1)
     * target = (1.3, 2.7, 3.1)
     * snapped = (1.0, 3.0, 3.0)
     * corrected delta = snapped - origin = (0.0, 1.0, 0.0) */
    vec3_t origin = {1.0f, 2.0f, 3.0f};
    vec3_t accum  = {0.3f, 0.7f, 0.1f};
    vec3_t result = snap_apply_position(&snap, origin, accum);
    ASSERT_FLOAT_NEAR(0.0f, result.x, 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, result.y, 0.001f);
    ASSERT_FLOAT_NEAR(0.0f, result.z, 0.001f);
    return 0;
}

static int test_snap_position_per_axis(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 1.0f;
    snap.axis_y[SNAP_POSITION] = false; /* Y unsnapped */
    /* origin = (0, 0, 0), accum = (1.7, 1.7, 1.7)
     * target = (1.7, 1.7, 1.7)
     * X snapped=2.0, Y passthrough=1.7, Z snapped=2.0 */
    vec3_t origin = {0.0f, 0.0f, 0.0f};
    vec3_t accum  = {1.7f, 1.7f, 1.7f};
    vec3_t result = snap_apply_position(&snap, origin, accum);
    ASSERT_FLOAT_NEAR(2.0f, result.x, 0.001f);
    ASSERT_FLOAT_NEAR(1.7f, result.y, 0.001f);
    ASSERT_FLOAT_NEAR(2.0f, result.z, 0.001f);
    return 0;
}

static int test_snap_position_half_grid(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_POSITION] = true;
    snap.grid_size[SNAP_POSITION] = 0.5f;
    /* origin = (0.1, 0, 0), accum = (0.2, 0, 0)
     * target = (0.3, 0, 0), snapped = (0.5, 0, 0)
     * delta = (0.4, 0, 0) */
    vec3_t origin = {0.1f, 0.0f, 0.0f};
    vec3_t accum  = {0.2f, 0.0f, 0.0f};
    vec3_t result = snap_apply_position(&snap, origin, accum);
    ASSERT_FLOAT_NEAR(0.4f, result.x, 0.001f);
    return 0;
}

/* ---- snap_apply_rotation: snap accumulated rotation in degrees ----------- */

static int test_snap_rotation_disabled(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    float rot = 12.0f;
    float result = snap_apply_rotation(&snap, rot, 0);
    ASSERT_FLOAT_NEAR(12.0f, result, 0.001f);
    return 0;
}

static int test_snap_rotation_rounds(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_ROTATION] = true;
    /* 12 degrees → snaps to 15 */
    float result = snap_apply_rotation(&snap, 12.0f, 0);
    ASSERT_FLOAT_NEAR(15.0f, result, 0.001f);
    /* 23 degrees → snaps to 15 (round to nearest 15) */
    result = snap_apply_rotation(&snap, 23.0f, 0);
    ASSERT_FLOAT_NEAR(30.0f, result, 0.001f);
    return 0;
}

/* ---- snap_apply_scale: snap absolute scale factor ----------------------- */

static int test_snap_scale_disabled(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    vec3_t orig_scale = {1.0f, 1.0f, 1.0f};
    vec3_t accum = {1.47f, 1.53f, 1.0f};
    vec3_t result = snap_apply_scale(&snap, orig_scale, accum);
    /* Passthrough */
    ASSERT_FLOAT_NEAR(1.47f, result.x, 0.001f);
    ASSERT_FLOAT_NEAR(1.53f, result.y, 0.001f);
    return 0;
}

static int test_snap_scale_rounds(void) {
    snap_state_t snap;
    snap_state_init(&snap);
    snap.enabled[SNAP_SCALE] = true;
    /* Default grid = 1.0
     * orig_scale = (1,1,1), accum = (1.47, 2.53, 1.0)
     * target = (1.47, 2.53, 1.0) (absolute scale after drag)
     * snapped = (1.0, 3.0, 1.0)
     * result factor = snapped / orig = (1.0, 3.0, 1.0) */
    vec3_t orig_scale = {1.0f, 1.0f, 1.0f};
    vec3_t accum = {1.47f, 2.53f, 1.0f};
    vec3_t result = snap_apply_scale(&snap, orig_scale, accum);
    ASSERT_FLOAT_NEAR(1.0f, result.x, 0.001f);
    ASSERT_FLOAT_NEAR(3.0f, result.y, 0.001f);
    ASSERT_FLOAT_NEAR(1.0f, result.z, 0.001f);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"snap_position_disabled",       test_snap_position_disabled},
    {"snap_position_rounds_target",  test_snap_position_rounds_target},
    {"snap_position_per_axis",       test_snap_position_per_axis},
    {"snap_position_half_grid",      test_snap_position_half_grid},
    {"snap_rotation_disabled",       test_snap_rotation_disabled},
    {"snap_rotation_rounds",         test_snap_rotation_rounds},
    {"snap_scale_disabled",          test_snap_scale_disabled},
    {"snap_scale_rounds",            test_snap_scale_rounds},
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
