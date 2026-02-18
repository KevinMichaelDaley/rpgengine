#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"
#include "ferrum/renderer/shader_uniforms.h"

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

#define ASSERT_FLOAT_EQ(exp, act)                                                                       \
    do {                                                                                                \
        float _diff = (float)(exp) - (float)(act);                                                      \
        if (_diff < 0.0f) {                                                                             \
            _diff = -_diff;                                                                             \
        }                                                                                               \
        if (_diff > 0.0001f) {                                                                          \
            fprintf(stderr, "ASSERT_FLOAT_EQ failed: %s:%d: expected %f got %f\n", __FILE__, __LINE__,  \
                    (float)(exp), (float)(act));                                                        \
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

    ctx->window = SDL_CreateWindow("p004_uniform_tests",
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

static shader_program_status_t build_program(shader_program_t *program) {
    const char *vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec3 in_pos;\n"
        "uniform mat4 u_mat;\n"
        "out vec3 v_color;\n"
        "void main() {\n"
        "    v_color = in_pos;\n"
        "    gl_Position = u_mat * vec4(in_pos, 1.0);\n"
        "}\n";
    const char *fs_src =
        "#version 330 core\n"
        "in vec3 v_color;\n"
        "uniform vec3 u_vec;\n"
        "uniform float u_float;\n"
        "uniform int u_int;\n"
        "uniform float u_scalar;\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "    float scaled = u_scalar + float(u_int) * 0.01;\n"
        "    out_color = vec4(v_color + u_vec * u_float, scaled);\n"
        "}\n";

    char log_buffer[512] = {0};
    return shader_program_create(program, &g_loader, vs_src, fs_src, log_buffer, sizeof(log_buffer));
}

static int test_uniform_uploads_and_cache(void) {
    shader_program_t program;
    ASSERT_INT_EQ(SHADER_PROGRAM_OK, build_program(&program));
    ASSERT_INT_EQ(SHADER_PROGRAM_OK, shader_program_bind(&program));

    shader_uniform_cache_t cache;
    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_cache_init(&cache, &program));
    ASSERT_INT_EQ(0, (int)shader_uniform_cache_count(&cache));

    float mat[16] = {
        1.0f, 0.0f, 0.0f, 0.0f,
        0.0f, 1.0f, 0.0f, 0.0f,
        0.0f, 0.0f, 1.0f, 0.0f,
        0.0f, 0.0f, 0.0f, 1.0f
    };
    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_set_mat4(&cache, &program, "u_mat", mat, 0));

    float vec[3] = {0.25f, 0.5f, 0.75f};
    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_set_vec3(&cache, &program, "u_vec", vec));

    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_set_float(&cache, &program, "u_float", 0.5f));
    ASSERT_INT_EQ(3, (int)shader_uniform_cache_count(&cache));

    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_set_float(&cache, &program, "u_float", 0.75f));
    ASSERT_INT_EQ(3, (int)shader_uniform_cache_count(&cache));

    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_set_int(&cache, &program, "u_int", 7));

    GLint location = glGetUniformLocation((GLuint)shader_program_handle(&program), "u_float");
    ASSERT_TRUE(location >= 0);
    float out_float[1] = {0.0f};
    glGetUniformfv((GLuint)shader_program_handle(&program), location, out_float);
    ASSERT_FLOAT_EQ(0.75f, out_float[0]);

    location = glGetUniformLocation((GLuint)shader_program_handle(&program), "u_int");
    ASSERT_TRUE(location >= 0);
    int out_int[1] = {0};
    glGetUniformiv((GLuint)shader_program_handle(&program), location, out_int);
    ASSERT_INT_EQ(7, out_int[0]);

    location = glGetUniformLocation((GLuint)shader_program_handle(&program), "u_mat");
    ASSERT_TRUE(location >= 0);
    float out_mat[16] = {0};
    glGetUniformfv((GLuint)shader_program_handle(&program), location, out_mat);
    ASSERT_FLOAT_EQ(1.0f, out_mat[0]);
    ASSERT_FLOAT_EQ(1.0f, out_mat[5]);
    ASSERT_FLOAT_EQ(1.0f, out_mat[10]);
    ASSERT_FLOAT_EQ(1.0f, out_mat[15]);

    shader_program_destroy(&program);
    return 0;
}

static int test_uniform_type_mismatch(void) {
    shader_program_t program;
    ASSERT_INT_EQ(SHADER_PROGRAM_OK, build_program(&program));
    ASSERT_INT_EQ(SHADER_PROGRAM_OK, shader_program_bind(&program));

    shader_uniform_cache_t cache;
    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_cache_init(&cache, &program));

    ASSERT_INT_EQ(SHADER_UNIFORM_OK, shader_uniform_set_int(&cache, &program, "u_scalar", 3));
    ASSERT_INT_EQ(SHADER_UNIFORM_ERR_TYPE_MISMATCH,
                  shader_uniform_set_float(&cache, &program, "u_scalar", 0.5f));

    shader_program_destroy(&program);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"uniform_uploads_and_cache", test_uniform_uploads_and_cache},
    {"uniform_type_mismatch", test_uniform_type_mismatch}
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
