#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/skinning_shader.h"

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

    ctx->window = SDL_CreateWindow("p004_skinning_tests",
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

static int test_skinning_shader_success_and_bind(void) {
    skinning_shader_t shader;
    char log_buffer[512] = {0};

    skinning_shader_status_t status = skinning_shader_create(&shader, &g_loader, log_buffer, sizeof(log_buffer));
    if (status != SKINNING_SHADER_OK) {
        fprintf(stderr, "skinning create failed: %s\n", log_buffer);
        return 1;
    }

    bone_palette_buffer_t palette;
    ASSERT_INT_EQ(BONE_PALETTE_OK,
                  bone_palette_buffer_init(&palette, &g_loader, 4u, 0u, 0, 0));

    ASSERT_INT_EQ(SKINNING_SHADER_OK, skinning_shader_bind(&shader, &palette));

    GLint bound = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &bound);
    ASSERT_INT_EQ((int)skinning_shader_program_handle(&shader), bound);

    GLint block_index = glGetUniformBlockIndex((GLuint)skinning_shader_program_handle(&shader), "BonePalette");
    ASSERT_TRUE(block_index >= 0);
    glUniformBlockBinding((GLuint)skinning_shader_program_handle(&shader), (GLuint)block_index, 0);
    GLint ubo_bound = 0;
    glGetIntegeri_v(GL_UNIFORM_BUFFER_BINDING, 0, &ubo_bound);
    ASSERT_INT_EQ((int)bone_palette_buffer_handle(&palette), ubo_bound);

    skinning_shader_destroy(&shader);
    bone_palette_buffer_destroy(&palette);
    return 0;
}

static int test_skinning_shader_link_failure(void) {
    skinning_shader_t shader;
    char log_buffer[64] = {0};

    ASSERT_INT_EQ(SKINNING_SHADER_ERR_LINK,
                  skinning_shader_create_from_source(&shader,
                                                     &g_loader,
                                                     "#version 330 core\nout vec3 v_color;\nvoid main() { v_color = vec3(1.0); gl_Position = vec4(0.0); }\n",
                                                     "#version 330 core\nin vec2 v_color;\nout vec4 out_color;\nvoid main() { out_color = vec4(v_color, 0.0, 1.0); }\n",
                                                     log_buffer,
                                                     sizeof(log_buffer)));
    ASSERT_TRUE(log_buffer[sizeof(log_buffer) - 1] == '\0');
    return 0;
}

static int test_skinning_attribute_semantics(void) {
    ASSERT_INT_EQ(0, (int)skinning_attribute_location(SKINNING_ATTRIBUTE_POSITION));
    ASSERT_INT_EQ(1, (int)skinning_attribute_location(SKINNING_ATTRIBUTE_NORMAL));
    ASSERT_INT_EQ(2, (int)skinning_attribute_location(SKINNING_ATTRIBUTE_TEXCOORD));
    ASSERT_INT_EQ(3, (int)skinning_attribute_location(SKINNING_ATTRIBUTE_BONE_WEIGHTS));
    ASSERT_INT_EQ(4, (int)skinning_attribute_location(SKINNING_ATTRIBUTE_BONE_INDICES));
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"skinning_shader_success_and_bind", test_skinning_shader_success_and_bind},
    {"skinning_shader_link_failure", test_skinning_shader_link_failure},
    {"skinning_attribute_semantics", test_skinning_attribute_semantics}
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
