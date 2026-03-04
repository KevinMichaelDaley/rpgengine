/**
 * @file p004_renderer_ubo_tests.c
 * @brief Tests for frame_params_ubo_t and instance_data_ubo_t.
 *
 * Requires an OpenGL 3.3 context (SDL2 + GLAD) for GL buffer operations.
 */

#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/ubo/frame_params_ubo.h"
#include "ferrum/renderer/ubo/instance_data_ubo.h"

/* ── Test macros ──────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, (int)(exp), (int)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_UINT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", \
                __FILE__, __LINE__, (unsigned)(exp), (unsigned)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps) do { \
    if (fabsf((float)(exp) - (float)(act)) > (eps)) { \
        fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: " \
                "expected %.6f got %.6f\n", \
                __FILE__, __LINE__, (double)(exp), (double)(act)); \
        return 1; \
    } \
} while (0)

/* ── GL context ───────────────────────────────────────────────────── */

struct gl_test_context {
    SDL_Window    *window;
    SDL_GLContext  context;
};

static gl_loader_t g_loader;

static void *sdl_get_proc_address(const char *name, void *user_data) {
    (void)user_data;
    return SDL_GL_GetProcAddress(name);
}

static int gl_test_context_init(struct gl_test_context *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,
                        SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS,
                        SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    ctx->window = SDL_CreateWindow("p004_ubo_tests",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   64, 64,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!ctx->window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    ctx->context = SDL_GL_CreateContext(ctx->window);
    if (!ctx->context) {
        fprintf(stderr, "SDL_GL_CreateContext failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    if (SDL_GL_MakeCurrent(ctx->window, ctx->context) != 0) {
        fprintf(stderr, "SDL_GL_MakeCurrent failed: %s\n", SDL_GetError());
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        fprintf(stderr, "gladLoadGLLoader failed\n");
        SDL_GL_DeleteContext(ctx->context);
        SDL_DestroyWindow(ctx->window);
        SDL_Quit();
        return -1;
    }
    (void)glGetError();

    g_loader.get_proc_address = sdl_get_proc_address;
    g_loader.user_data = NULL;
    return 0;
}

static void gl_test_context_cleanup(struct gl_test_context *ctx) {
    SDL_GL_DeleteContext(ctx->context);
    SDL_DestroyWindow(ctx->window);
    SDL_Quit();
}

/* ── Identity matrix helper ───────────────────────────────────────── */

static void make_identity_(float m[16]) {
    memset(m, 0, 16 * sizeof(float));
    m[0] = 1.0f; m[5] = 1.0f; m[10] = 1.0f; m[15] = 1.0f;
}

/* ═══════════════════════════════════════════════════════════════════
 *  FRAME PARAMS UBO — HAPPY PATH
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init creates GL buffer ───────────────────────────────── */
static int test_frame_params_init(void) {
    frame_params_ubo_t ubo;
    int rc = frame_params_ubo_init(&ubo, &g_loader, FRAME_PARAMS_BINDING);
    ASSERT_INT_EQ(FRAME_PARAMS_UBO_OK, rc);
    ASSERT_TRUE(ubo.handle != 0);
    frame_params_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: upload and readback via staging ──────────────────────── */
static int test_frame_params_upload(void) {
    frame_params_ubo_t ubo;
    frame_params_ubo_init(&ubo, &g_loader, FRAME_PARAMS_BINDING);

    frame_params_t params;
    memset(&params, 0, sizeof(params));
    make_identity_(params.view);
    make_identity_(params.proj);
    make_identity_(params.view_proj);
    params.camera_pos[0] = 1.0f;
    params.camera_pos[1] = 2.0f;
    params.camera_pos[2] = 3.0f;
    params.time = 42.0f;

    int rc = frame_params_ubo_upload(&ubo, &params);
    ASSERT_INT_EQ(FRAME_PARAMS_UBO_OK, rc);

    /* Verify no GL error. */
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    frame_params_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: bind to binding point ────────────────────────────────── */
static int test_frame_params_bind(void) {
    frame_params_ubo_t ubo;
    frame_params_ubo_init(&ubo, &g_loader, FRAME_PARAMS_BINDING);

    frame_params_t params;
    memset(&params, 0, sizeof(params));
    make_identity_(params.view);
    make_identity_(params.proj);
    make_identity_(params.view_proj);
    frame_params_ubo_upload(&ubo, &params);

    int rc = frame_params_ubo_bind(&ubo);
    ASSERT_INT_EQ(FRAME_PARAMS_UBO_OK, rc);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    frame_params_ubo_destroy(&ubo);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  INSTANCE DATA UBO — HAPPY PATH
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init with configurable capacity ──────────────────────── */
static int test_instance_init(void) {
    instance_data_ubo_t ubo;
    int rc = instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 512);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_OK, rc);
    ASSERT_TRUE(ubo.handle != 0);
    ASSERT_UINT_EQ(512, ubo.capacity);
    instance_data_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: init with large capacity ─────────────────────────────── */
static int test_instance_init_large(void) {
    instance_data_ubo_t ubo;
    int rc = instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 4096);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_OK, rc);
    ASSERT_UINT_EQ(4096, ubo.capacity);
    instance_data_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: upload a batch of instances ──────────────────────────── */
static int test_instance_upload(void) {
    instance_data_ubo_t ubo;
    instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 64);

    /* Build 4 instances with identity model matrices. */
    instance_data_t instances[4];
    for (int i = 0; i < 4; ++i) {
        make_identity_(instances[i].model);
        make_identity_(instances[i].model_inv_transpose);
        instances[i].entity_id = (uint32_t)i;
        memset(instances[i]._pad, 0, sizeof(instances[i]._pad));
    }

    int rc = instance_data_ubo_upload(&ubo, instances, 4);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_OK, rc);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    instance_data_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: bind to binding point ────────────────────────────────── */
static int test_instance_bind(void) {
    instance_data_ubo_t ubo;
    instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 64);

    instance_data_t inst;
    make_identity_(inst.model);
    make_identity_(inst.model_inv_transpose);
    inst.entity_id = 0;
    memset(inst._pad, 0, sizeof(inst._pad));
    instance_data_ubo_upload(&ubo, &inst, 1);

    int rc = instance_data_ubo_bind(&ubo);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_OK, rc);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);

    instance_data_ubo_destroy(&ubo);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  EDGE CASES
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: instance upload count=0 is a no-op ───────────────────── */
static int test_instance_upload_zero(void) {
    instance_data_ubo_t ubo;
    instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 64);

    int rc = instance_data_ubo_upload(&ubo, NULL, 0);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_OK, rc);

    instance_data_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: instance upload exceeding capacity returns error ─────── */
static int test_instance_upload_overflow(void) {
    instance_data_ubo_t ubo;
    instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 2);

    instance_data_t instances[3];
    memset(instances, 0, sizeof(instances));

    int rc = instance_data_ubo_upload(&ubo, instances, 3);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_ERR_FULL, rc);

    instance_data_ubo_destroy(&ubo);
    return 0;
}

/* ── Test: frame_params struct is std140-compatible size ─────────── */
static int test_frame_params_size(void) {
    /* std140 alignment: must be multiple of 16 bytes. */
    ASSERT_TRUE((sizeof(frame_params_t) % 16) == 0);
    return 0;
}

/* ── Test: instance_data struct is std140-compatible size ────────── */
static int test_instance_data_size(void) {
    /* Must be multiple of 16 bytes for std140 array stride. */
    ASSERT_TRUE((sizeof(instance_data_t) % 16) == 0);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  FAILURE MODES
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: frame_params init NULL ───────────────────────────────── */
static int test_frame_params_init_null(void) {
    int rc = frame_params_ubo_init(NULL, &g_loader, FRAME_PARAMS_BINDING);
    ASSERT_INT_EQ(FRAME_PARAMS_UBO_ERR_INVALID, rc);
    return 0;
}

/* ── Test: frame_params init NULL loader ────────────────────────── */
static int test_frame_params_init_null_loader(void) {
    frame_params_ubo_t ubo;
    int rc = frame_params_ubo_init(&ubo, NULL, FRAME_PARAMS_BINDING);
    ASSERT_INT_EQ(FRAME_PARAMS_UBO_ERR_INVALID, rc);
    return 0;
}

/* ── Test: instance init NULL ───────────────────────────────────── */
static int test_instance_init_null(void) {
    int rc = instance_data_ubo_init(NULL, &g_loader, INSTANCE_DATA_BINDING, 64);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_ERR_INVALID, rc);
    return 0;
}

/* ── Test: instance init zero capacity ──────────────────────────── */
static int test_instance_init_zero(void) {
    instance_data_ubo_t ubo;
    int rc = instance_data_ubo_init(&ubo, &g_loader, INSTANCE_DATA_BINDING, 0);
    ASSERT_INT_EQ(INSTANCE_DATA_UBO_ERR_INVALID, rc);
    return 0;
}

/* ── Test: destroy NULL is safe ─────────────────────────────────── */
static int test_destroy_null(void) {
    frame_params_ubo_destroy(NULL);
    instance_data_ubo_destroy(NULL);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Test runner
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct { const char *name; int (*fn)(void); } test_entry_t;

static const test_entry_t TESTS[] = {
    /* Frame params */
    {"frame_params_init",            test_frame_params_init},
    {"frame_params_upload",          test_frame_params_upload},
    {"frame_params_bind",            test_frame_params_bind},
    /* Instance data */
    {"instance_init",                test_instance_init},
    {"instance_init_large",          test_instance_init_large},
    {"instance_upload",              test_instance_upload},
    {"instance_bind",                test_instance_bind},
    /* Edge cases */
    {"instance_upload_zero",         test_instance_upload_zero},
    {"instance_upload_overflow",     test_instance_upload_overflow},
    {"frame_params_size",            test_frame_params_size},
    {"instance_data_size",           test_instance_data_size},
    /* Failure modes */
    {"frame_params_init_null",       test_frame_params_init_null},
    {"frame_params_init_null_loader",test_frame_params_init_null_loader},
    {"instance_init_null",           test_instance_init_null},
    {"instance_init_zero",           test_instance_init_zero},
    {"destroy_null",                 test_destroy_null},
};

int main(void) {
    struct gl_test_context ctx = {0};
    if (gl_test_context_init(&ctx) != 0) return 1;

    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("  OK %s\n", TESTS[i].name);
            ++passed;
        } else {
            printf("FAIL %s\n", TESTS[i].name);
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);

    gl_test_context_cleanup(&ctx);
    return (passed == total) ? 0 : 1;
}
