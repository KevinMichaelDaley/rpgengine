/**
 * @file p004_renderer_texture_tests.c
 * @brief Unit tests for the texture_t wrapper against a real hidden GL context.
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/texture.h"
#include "ferrum/renderer/texture_format.h"

#define ASSERT_TRUE(cond)                                                     \
    do {                                                                      \
        if (!(cond)) {                                                        \
            fprintf(stderr, "ASSERT failed %s:%d: %s\n", __FILE__, __LINE__,  \
                    #cond);                                                   \
            return 1;                                                         \
        }                                                                     \
    } while (0)

static gl_loader_t g_loader;

static void *sdl_get_proc(const char *name, void *user) {
    (void)user;
    return SDL_GL_GetProcAddress(name);
}

struct gl_ctx {
    SDL_Window *window;
    SDL_GLContext context;
};

static int gl_ctx_init(struct gl_ctx *c) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return -1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    c->window = SDL_CreateWindow("tex_tests", 0, 0, 64, 64,
                                 SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (!c->window) { SDL_Quit(); return -1; }
    c->context = SDL_GL_CreateContext(c->window);
    if (!c->context) { SDL_DestroyWindow(c->window); SDL_Quit(); return -1; }
    SDL_GL_MakeCurrent(c->window, c->context);
    if (!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)) {
        SDL_GL_DeleteContext(c->context); SDL_DestroyWindow(c->window);
        SDL_Quit(); return -1;
    }
    (void)glGetError();
    g_loader.get_proc_address = sdl_get_proc;
    g_loader.user_data = NULL;
    return 0;
}

static void gl_ctx_shutdown(struct gl_ctx *c) {
    if (c->context) SDL_GL_DeleteContext(c->context);
    if (c->window) SDL_DestroyWindow(c->window);
    SDL_Quit();
}

/* Create yields a live handle; destroy releases it. */
static int test_create_destroy(void) {
    texture_t t;
    ASSERT_TRUE(texture_create(&t, &g_loader) == TEXTURE_OK);
    ASSERT_TRUE(texture_handle(&t) != 0u);
    texture_destroy(&t);
    ASSERT_TRUE(texture_handle(&t) == 0u);
    return 0;
}

/* Invalid args are rejected without touching GL. */
static int test_invalid_args(void) {
    texture_t t;
    ASSERT_TRUE(texture_create(NULL, &g_loader) == TEXTURE_ERR_INVALID);
    ASSERT_TRUE(texture_create(&t, NULL) == TEXTURE_ERR_MISSING_GL);
    ASSERT_TRUE(texture_handle(NULL) == 0u);
    return 0;
}

/* Upload each format (colour + data + float) without provoking a GL error. */
static int test_upload_formats(void) {
    texture_t t;
    ASSERT_TRUE(texture_create(&t, &g_loader) == TEXTURE_OK);
    uint8_t rgba[4 * 4 * 4];
    memset(rgba, 128, sizeof(rgba));
    float rgbf[4 * 4 * 3];
    for (size_t i = 0; i < sizeof(rgbf) / sizeof(rgbf[0]); ++i) rgbf[i] = 0.5f;

    ASSERT_TRUE(texture_upload_2d(&t, TEXTURE_FORMAT_R8, 4, 4, rgba, false) == TEXTURE_OK);
    ASSERT_TRUE(texture_upload_2d(&t, TEXTURE_FORMAT_RGB8, 4, 4, rgba, false) == TEXTURE_OK);
    ASSERT_TRUE(texture_upload_2d(&t, TEXTURE_FORMAT_SRGB8_A8, 4, 4, rgba, true) == TEXTURE_OK);
    ASSERT_TRUE(texture_upload_2d(&t, TEXTURE_FORMAT_RGB32F, 4, 4, rgbf, false) == TEXTURE_OK);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);
    texture_destroy(&t);
    return 0;
}

/* Upload/sampler/bind reject invalid input; valid calls stay GL-clean. */
static int test_sampler_bind_and_errors(void) {
    texture_t t;
    ASSERT_TRUE(texture_create(&t, &g_loader) == TEXTURE_OK);
    uint8_t px[4] = { 200, 100, 50, 255 };
    ASSERT_TRUE(texture_upload_2d(&t, TEXTURE_FORMAT_RGBA8, 0, 4, px, false) == TEXTURE_ERR_INVALID);
    ASSERT_TRUE(texture_upload_2d(&t, TEXTURE_FORMAT_RGBA8, 1, 1, px, false) == TEXTURE_OK);
    ASSERT_TRUE(texture_set_sampler(&t, GL_LINEAR, GL_LINEAR, GL_CLAMP_TO_EDGE,
                                    GL_CLAMP_TO_EDGE) == TEXTURE_OK);
    ASSERT_TRUE(texture_bind(&t, 3u) == TEXTURE_OK);
    ASSERT_TRUE(glGetError() == GL_NO_ERROR);
    texture_destroy(&t);
    return 0;
}

/* Format resolver maps known formats and rejects the sentinel. */
static int test_format_resolve(void) {
    for (int f = 0; f < TEXTURE_FORMAT_COUNT; ++f) {
        uint32_t a = 0, b = 0, c = 0;
        ASSERT_TRUE(texture_format_resolve((texture_format_t)f, &a, &b, &c));
        ASSERT_TRUE(a != 0 && b != 0 && c != 0);
    }
    uint32_t a, b, c;
    ASSERT_TRUE(!texture_format_resolve(TEXTURE_FORMAT_COUNT, &a, &b, &c));
    return 0;
}

struct tc { const char *name; int (*fn)(void); };
static struct tc TESTS[] = {
    { "create_destroy", test_create_destroy },
    { "invalid_args", test_invalid_args },
    { "upload_formats", test_upload_formats },
    { "sampler_bind_and_errors", test_sampler_bind_and_errors },
    { "format_resolve", test_format_resolve },
};

int main(void) {
    struct gl_ctx ctx;
    if (gl_ctx_init(&ctx) != 0) {
        fprintf(stderr, "no GL context; skipping texture tests\n");
        return 0; /* headless CI without a display: treat as skip */
    }
    int failed = 0;
    for (size_t i = 0; i < sizeof(TESTS) / sizeof(TESTS[0]); ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int r = TESTS[i].fn();
        printf(r == 0 ? "OK   %s\n" : "FAIL %s\n", TESTS[i].name);
        failed += (r != 0);
    }
    gl_ctx_shutdown(&ctx);
    printf("%s (%d failed)\n", failed ? "FAILED" : "PASSED", failed);
    return failed ? 1 : 0;
}
