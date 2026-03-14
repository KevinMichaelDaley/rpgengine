/**
 * @file shading_mode_tests.c
 * @brief Tests for per-viewport shading mode system.
 *
 * Tests cover shading mode enum cycling, name strings, and
 * integration with viewport state initialization.
 */

#include "ferrum/editor/viewport/viewport_shading.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"

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

#define ASSERT_NEQ(a, b) do { \
    if ((a) == (b)) { \
        printf("  ASSERT_NEQ FAILED: %s == %d (line %d)\n", \
               #a, (int)(a), __LINE__); \
        return false; \
    } \
} while (0)

#define RUN(fn) do { \
    printf("  %s ... ", #fn); \
    if (fn()) { printf("ok\n"); s_pass++; } \
    else { printf("FAIL\n"); s_fail++; } \
} while (0)

/* ---- Shading mode enum tests ---- */

/** Default mode is SHADED (value 0). */
static bool test_default_mode(void) {
    ASSERT_EQ(SHADING_MODE_SHADED, 0);
    return true;
}

/** All modes have distinct values. */
static bool test_mode_values(void) {
    ASSERT_NEQ(SHADING_MODE_SHADED, SHADING_MODE_MATCAP);
    ASSERT_NEQ(SHADING_MODE_MATCAP, SHADING_MODE_UNLIT);
    ASSERT_NEQ(SHADING_MODE_UNLIT, SHADING_MODE_WIREFRAME);
    ASSERT_NEQ(SHADING_MODE_WIREFRAME, SHADING_MODE_SHADED);
    return true;
}

/** Count is correct. */
static bool test_mode_count(void) {
    ASSERT_EQ(SHADING_MODE_COUNT, 4);
    return true;
}

/** Cycling wraps from last to first. */
static bool test_mode_next_wraps(void) {
    shading_mode_t m = SHADING_MODE_SHADED;
    m = shading_mode_next(m);
    ASSERT_EQ(m, SHADING_MODE_MATCAP);
    m = shading_mode_next(m);
    ASSERT_EQ(m, SHADING_MODE_UNLIT);
    m = shading_mode_next(m);
    ASSERT_EQ(m, SHADING_MODE_WIREFRAME);
    m = shading_mode_next(m);
    ASSERT_EQ(m, SHADING_MODE_SHADED);
    return true;
}

/** All modes have non-empty display names. */
static bool test_mode_names(void) {
    ASSERT_STREQ(shading_mode_name(SHADING_MODE_SHADED), "Shaded");
    ASSERT_STREQ(shading_mode_name(SHADING_MODE_MATCAP), "Matcap");
    ASSERT_STREQ(shading_mode_name(SHADING_MODE_UNLIT), "Unlit");
    ASSERT_STREQ(shading_mode_name(SHADING_MODE_WIREFRAME), "Wire");
    return true;
}

/** Invalid mode returns "?" name. */
static bool test_invalid_mode_name(void) {
    ASSERT_STREQ(shading_mode_name((shading_mode_t)99), "?");
    return true;
}

/* ---- Viewport state integration ---- */

/** Viewport state initializes shading mode to SHADED. */
static bool test_viewport_state_init_shading(void) {
    viewport_state_t state;
    viewport_state_init(&state);
    ASSERT_EQ(state.shading_mode, SHADING_MODE_SHADED);
    return true;
}

/** Copy camera preserves shading mode. */
static bool test_viewport_state_copy_shading(void) {
    viewport_state_t src, dst;
    viewport_state_init(&src);
    viewport_state_init(&dst);
    src.shading_mode = SHADING_MODE_WIREFRAME;
    viewport_state_copy_camera(&dst, &src);
    ASSERT_EQ(dst.shading_mode, SHADING_MODE_WIREFRAME);
    return true;
}

/** Reset restores shading mode to default. */
static bool test_viewport_state_reset_shading(void) {
    viewport_state_t state;
    viewport_state_init(&state);
    state.shading_mode = SHADING_MODE_MATCAP;
    viewport_state_reset(&state);
    ASSERT_EQ(state.shading_mode, SHADING_MODE_SHADED);
    return true;
}

/* ---- Entry point ---- */

int main(void) {
    printf("shading_mode_tests\n");

    RUN(test_default_mode);
    RUN(test_mode_values);
    RUN(test_mode_count);
    RUN(test_mode_next_wraps);
    RUN(test_mode_names);
    RUN(test_invalid_mode_name);
    RUN(test_viewport_state_init_shading);
    RUN(test_viewport_state_copy_shading);
    RUN(test_viewport_state_reset_shading);

    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? EXIT_FAILURE : EXIT_SUCCESS;
}
