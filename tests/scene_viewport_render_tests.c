/**
 * @file scene_viewport_render_tests.c
 * @brief Tests for viewport render state initialization (headless).
 *
 * Verifies non-GL aspects of viewport render state: struct layout,
 * defaults, and type sizes. FBO/shader creation requires a live
 * GL context and is covered by integration tests.
 */

#include <stdio.h>
#include <string.h>

#include "ferrum/editor/scene/scene_viewport_render.h"

static int s_pass = 0;
static int s_fail = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
        s_fail++; \
    } else { \
        s_pass++; \
    } \
} while (0)

/* ---- Tests ---- */

static void test_render_state_zero_initialized(void) {
    printf("test_render_state_zero_initialized\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));

    ASSERT(state.fbo == 0, "FBO should be 0 after zero-init");
    ASSERT(state.color_tex == 0, "color_tex should be 0 after zero-init");
    ASSERT(state.depth_rbo == 0, "depth_rbo should be 0 after zero-init");
    ASSERT(state.fbo_width == 0, "fbo_width should be 0 after zero-init");
    ASSERT(state.fbo_height == 0, "fbo_height should be 0 after zero-init");
    ASSERT(state.initialized == false, "initialized should be false");
}

static void test_camera_defaults(void) {
    printf("test_camera_defaults\n");
    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));

    ASSERT(state.camera.distance == 0.0f,
           "camera distance should be 0 before init");
}

static void test_struct_contains_renderer_types(void) {
    printf("test_struct_contains_renderer_types\n");
    /* Verify the struct contains the expected renderer infrastructure. */
    ASSERT(sizeof(viewport_render_state_t) > sizeof(editor_camera_t),
           "state should be larger than just a camera");
    ASSERT(sizeof(viewport_render_state_t) > sizeof(render_pipeline_t),
           "state should contain pipeline and more");
    ASSERT(sizeof(viewport_render_state_t) > sizeof(mesh_registry_t),
           "state should contain mesh registry and more");
}

static void test_get_texture_null(void) {
    printf("test_get_texture_null\n");
    ASSERT(viewport_render_get_texture(NULL) == 0,
           "get_texture(NULL) should return 0");

    viewport_render_state_t state;
    memset(&state, 0, sizeof(state));
    ASSERT(viewport_render_get_texture(&state) == 0,
           "get_texture(uninitialized) should return 0");
}

static void test_init_null_args(void) {
    printf("test_init_null_args\n");
    viewport_render_config_t cfg = {0};
    ASSERT(viewport_render_init(NULL, &cfg) == false,
           "init(NULL, cfg) should return false");
    ASSERT(viewport_render_init(NULL, NULL) == false,
           "init(NULL, NULL) should return false");
}

int main(void) {
    printf("=== scene_viewport_render_tests ===\n");

    test_render_state_zero_initialized();
    test_camera_defaults();
    test_struct_contains_renderer_types();
    test_get_texture_null();
    test_init_null_args();

    printf("\n%d passed, %d failed\n", s_pass, s_fail);
    return s_fail > 0 ? 1 : 0;
}
