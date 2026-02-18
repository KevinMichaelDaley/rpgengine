#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/gl_loader.h"

#define ASSERT_TRUE(cond)                                                                               \
    do {                                                                                                \
        if (!(cond)) {                                                                                  \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                         \
    do {                                                                                                \
        if ((exp) != (act)) {                                                                           \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", __FILE__, __LINE__,    \
                    (int)(exp), (int)(act));                                                            \
            return 1;                                                                                   \
        }                                                                                               \
    } while (0)

struct gl_test_context {
    SDL_Window *window;
    SDL_GLContext context;
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
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    ctx->window = SDL_CreateWindow("p004_palette_tests",
                                   SDL_WINDOWPOS_UNDEFINED,
                                   SDL_WINDOWPOS_UNDEFINED,
                                   64,
                                   64,
                                   SDL_WINDOW_OPENGL | SDL_WINDOW_HIDDEN);
    if (ctx->window == NULL) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return -1;
    }
    ctx->context = SDL_GL_CreateContext(ctx->window);
    if (ctx->context == NULL) {
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
        fprintf(stderr, "gladLoadGLLoader failed
");
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

static void gl_test_context_shutdown(struct gl_test_context *ctx) {
    if (ctx->context != NULL) {
        SDL_GL_DeleteContext(ctx->context);
    }
    if (ctx->window != NULL) {
        SDL_DestroyWindow(ctx->window);
    }
    SDL_Quit();
}

static int test_palette_upload_bind_ssbo(void) {
    bone_palette_buffer_t palette;
    ASSERT_INT_EQ(BONE_PALETTE_OK,
                  bone_palette_buffer_init(&palette, &g_loader, 4u, 2u, 1, 0));
    ASSERT_INT_EQ(BONE_PALETTE_BUFFER_SSBO, (int)bone_palette_buffer_type(&palette));

    float data[BONE_PALETTE_MATRIX_FLOATS * 4] = {0};
    ASSERT_INT_EQ(BONE_PALETTE_OK, bone_palette_buffer_update(&palette, data, sizeof(data)));
    ASSERT_INT_EQ(BONE_PALETTE_OK, bone_palette_buffer_bind(&palette));

    GLint bound = 0;
    glGetIntegeri_v(GL_SHADER_STORAGE_BUFFER_BINDING, 2, &bound);
    ASSERT_INT_EQ((int)bone_palette_buffer_handle(&palette), bound);

    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bone_palette_buffer_handle(&palette));
    GLint buffer_size = 0;
    glGetBufferParameteriv(GL_SHADER_STORAGE_BUFFER, GL_BUFFER_SIZE, &buffer_size);
    ASSERT_INT_EQ((int)sizeof(data), buffer_size);

    bone_palette_buffer_destroy(&palette);
    return 0;
}

static int test_palette_fallback_to_ubo(void) {
    bone_palette_buffer_t palette;
    ASSERT_INT_EQ(BONE_PALETTE_OK,
                  bone_palette_buffer_init(&palette, &g_loader, 2u, 1u, 0, 0));
    ASSERT_INT_EQ(BONE_PALETTE_BUFFER_UBO, (int)bone_palette_buffer_type(&palette));

    ASSERT_INT_EQ(BONE_PALETTE_OK, bone_palette_buffer_bind(&palette));

    GLint bound = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 1, &bound);
    ASSERT_INT_EQ((int)bone_palette_buffer_handle(&palette), bound);

    bone_palette_buffer_destroy(&palette);
    return 0;
}

static int test_palette_size_limit(void) {
    bone_palette_buffer_t palette;
    ASSERT_INT_EQ(BONE_PALETTE_OK,
                  bone_palette_buffer_init(&palette, &g_loader, 1u, 0u, 0, 0));

    float data[BONE_PALETTE_MATRIX_FLOATS * 2] = {0};
    ASSERT_INT_EQ(BONE_PALETTE_ERR_TOO_LARGE, bone_palette_buffer_update(&palette, data, sizeof(data)));

    bone_palette_buffer_destroy(&palette);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"palette_upload_bind_ssbo", test_palette_upload_bind_ssbo},
    {"palette_fallback_to_ubo", test_palette_fallback_to_ubo},
    {"palette_size_limit", test_palette_size_limit}
};

int main(void) {
    struct gl_test_context ctx = {0};
    if (gl_test_context_init(&ctx) != 0) {
        return 1;
    }

    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("OK %s\n", TESTS[i].name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", TESTS[i].name, rc);
            break;
        }
    }

    gl_test_context_shutdown(&ctx);

    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
