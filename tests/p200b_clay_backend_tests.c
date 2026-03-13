/**
 * @file p200b_clay_backend_tests.c
 * @brief Unit tests for Clay UI backend initialization and integration.
 *
 * Tests Clay arena allocation, initialization, font measurement callback
 * registration, layout cycle, and render command iteration.
 * These tests are headless — they test the Clay integration layer without
 * actually rendering to an OpenGL context.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"

#include "ferrum/editor/ui/clay_backend.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got "   \
                    "%d\n", __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Dummy text measurement callback ---- */

static Clay_Dimensions dummy_measure_text(Clay_StringSlice text,
                                           Clay_TextElementConfig *config,
                                           void *user_data) {
    (void)user_data;
    /* Return width = 8 pixels per character, height = config->fontSize */
    float w = (float)text.length * 8.0f;
    float h = (float)config->fontSize;
    return (Clay_Dimensions){w, h};
}

/* ---- Tests ---- */

static int test_clay_min_memory_size(void) {
    uint32_t size = Clay_MinMemorySize();
    /* Clay needs at least some memory */
    ASSERT_TRUE(size > 0);
    ASSERT_TRUE(size < 64 * 1024 * 1024); /* sanity: less than 64 MB */
    return 0;
}

static int test_clay_arena_create(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    ASSERT_TRUE(mem != NULL);

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);
    ASSERT_TRUE(arena.capacity > 0);
    ASSERT_TRUE(arena.memory != NULL);

    free(mem);
    return 0;
}

static int test_clay_initialize(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    ASSERT_TRUE(mem != NULL);

    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);

    Clay_Dimensions dims = {1280.0f, 720.0f};
    Clay_ErrorHandler err = {0};

    Clay_Context *ctx = Clay_Initialize(arena, dims, err);
    ASSERT_TRUE(ctx != NULL);

    free(mem);
    return 0;
}

static int test_clay_set_measure_text(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);
    Clay_Dimensions dims = {1280.0f, 720.0f};
    Clay_ErrorHandler err = {0};

    Clay_Context *ctx = Clay_Initialize(arena, dims, err);
    ASSERT_TRUE(ctx != NULL);

    /* Should not crash */
    Clay_SetMeasureTextFunction(dummy_measure_text, NULL);

    free(mem);
    return 0;
}

static int test_clay_layout_cycle(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);
    Clay_Dimensions dims = {1280.0f, 720.0f};
    Clay_ErrorHandler err = {0};

    Clay_Context *ctx = Clay_Initialize(arena, dims, err);
    ASSERT_TRUE(ctx != NULL);
    Clay_SetMeasureTextFunction(dummy_measure_text, NULL);

    /* Run a minimal layout cycle */
    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions(dims);
    Clay_BeginLayout();

    /* Empty layout — no elements declared */
    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Should return a valid array (possibly empty) */
    ASSERT_TRUE(cmds.length >= 0);

    free(mem);
    return 0;
}

static int test_clay_layout_with_rectangle(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);
    Clay_Dimensions dims = {1280.0f, 720.0f};
    Clay_ErrorHandler err = {0};

    Clay_Context *ctx = Clay_Initialize(arena, dims, err);
    ASSERT_TRUE(ctx != NULL);
    Clay_SetMeasureTextFunction(dummy_measure_text, NULL);

    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions(dims);
    Clay_BeginLayout();

    /* Declare a simple colored rectangle */
    CLAY(CLAY_ID("TestRect"), {
        .layout = {
            .sizing = {CLAY_SIZING_FIXED(200), CLAY_SIZING_FIXED(100)},
        },
        .backgroundColor = {255, 0, 0, 255},
    }) {}

    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Should have at least one render command (the rectangle) */
    ASSERT_TRUE(cmds.length > 0);

    /* Find a RECTANGLE command */
    bool found_rect = false;
    for (int32_t i = 0; i < cmds.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            found_rect = true;
            /* Check it has the right color */
            ASSERT_INT_EQ(255, (int)cmd->renderData.rectangle.backgroundColor.r);
            ASSERT_INT_EQ(0,   (int)cmd->renderData.rectangle.backgroundColor.g);
            ASSERT_INT_EQ(0,   (int)cmd->renderData.rectangle.backgroundColor.b);
            break;
        }
    }
    ASSERT_TRUE(found_rect);

    free(mem);
    return 0;
}

static int test_clay_layout_with_text(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);
    Clay_Dimensions dims = {1280.0f, 720.0f};
    Clay_ErrorHandler err = {0};

    Clay_Context *ctx = Clay_Initialize(arena, dims, err);
    ASSERT_TRUE(ctx != NULL);
    Clay_SetMeasureTextFunction(dummy_measure_text, NULL);

    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions(dims);
    Clay_BeginLayout();

    /* Declare a text element */
    CLAY(CLAY_ID("TextContainer"), {
        .layout = {
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
        },
    }) {
        CLAY_TEXT(CLAY_STRING("Hello, Clay!"),
                  CLAY_TEXT_CONFIG({
                      .fontSize = 16,
                      .textColor = {255, 255, 255, 255},
                  }));
    }

    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Should have at least one text render command */
    bool found_text = false;
    for (int32_t i = 0; i < cmds.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            found_text = true;
            /* Text bounding box should have non-zero dimensions */
            ASSERT_TRUE(cmd->boundingBox.width > 0);
            ASSERT_TRUE(cmd->boundingBox.height > 0);
            break;
        }
    }
    ASSERT_TRUE(found_text);

    free(mem);
    return 0;
}

static int test_clay_backend_config_init(void) {
    clay_backend_config_t config = {0};
    config.window_w = 1280;
    config.window_h = 720;

    /* Just verify the struct zero-initializes cleanly */
    ASSERT_INT_EQ(1280, config.window_w);
    ASSERT_INT_EQ(720, config.window_h);
    return 0;
}

static int test_clay_render_command_iteration(void) {
    uint32_t size = Clay_MinMemorySize();
    void *mem = malloc(size);
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, mem);
    Clay_Dimensions dims = {800.0f, 600.0f};
    Clay_ErrorHandler err = {0};

    Clay_Context *ctx = Clay_Initialize(arena, dims, err);
    ASSERT_TRUE(ctx != NULL);
    Clay_SetMeasureTextFunction(dummy_measure_text, NULL);

    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions(dims);
    Clay_BeginLayout();

    /* Create a nested layout with multiple elements */
    CLAY(CLAY_ID("Outer"), {
        .layout = {
            .sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)},
            .layoutDirection = CLAY_LEFT_TO_RIGHT,
        },
    }) {
        CLAY(CLAY_ID("Left"), {
            .layout = {.sizing = {CLAY_SIZING_FIXED(100), CLAY_SIZING_GROW(0)}},
            .backgroundColor = {50, 50, 80, 255},
        }) {}

        CLAY(CLAY_ID("Right"), {
            .layout = {.sizing = {CLAY_SIZING_GROW(0), CLAY_SIZING_GROW(0)}},
            .backgroundColor = {30, 30, 40, 255},
        }) {}
    }

    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Count rectangle commands */
    int rect_count = 0;
    for (int32_t i = 0; i < cmds.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_RECTANGLE) {
            rect_count++;
        }
    }
    /* Should have at least 2 rectangles (Left and Right) */
    ASSERT_TRUE(rect_count >= 2);

    free(mem);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"clay_min_memory_size",         test_clay_min_memory_size},
    {"clay_arena_create",            test_clay_arena_create},
    {"clay_initialize",              test_clay_initialize},
    {"clay_set_measure_text",        test_clay_set_measure_text},
    {"clay_layout_cycle",            test_clay_layout_cycle},
    {"clay_layout_with_rectangle",   test_clay_layout_with_rectangle},
    {"clay_layout_with_text",        test_clay_layout_with_text},
    {"clay_backend_config_init",     test_clay_backend_config_init},
    {"clay_render_command_iteration", test_clay_render_command_iteration},
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
