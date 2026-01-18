#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/shader_program.h"

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

    ctx->window = SDL_CreateWindow("p004_shader_tests",
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

    glewExperimental = GL_TRUE;
    GLenum glew_status = glewInit();
    if (glew_status != GLEW_OK) {
        fprintf(stderr, "glewInit failed: %s\n", glewGetErrorString(glew_status));
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

static int test_shader_compile_link_success(void) {
    const char *vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec3 in_pos;\n"
        "void main() {\n"
        "    gl_Position = vec4(in_pos, 1.0);\n"
        "}\n";
    const char *fs_src =
        "#version 330 core\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = vec4(1.0, 0.5, 0.25, 1.0);\n"
        "}\n";

    shader_program_t program;
    char log_buffer[512] = {0};
    shader_program_status_t status = shader_program_create(&program, &g_loader, vs_src, fs_src,
                                                            log_buffer, sizeof(log_buffer));
    ASSERT_INT_EQ(SHADER_PROGRAM_OK, status);
    ASSERT_TRUE(shader_program_handle(&program) != 0u);

    status = shader_program_bind(&program);
    ASSERT_INT_EQ(SHADER_PROGRAM_OK, status);

    GLint bound = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &bound);
    ASSERT_INT_EQ((int)shader_program_handle(&program), bound);

    shader_program_destroy(&program);
    return 0;
}

static int test_shader_compile_failure_reports_log(void) {
    const char *vs_src =
        "#version 330 core\n"
        "layout(location=0) in vec3 in_pos;\n"
        "void main() {\n"
        "    gl_Position = vec4(in_pos, 1.0;\n"
        "}\n";
    const char *fs_src =
        "#version 330 core\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = vec4(1.0);\n"
        "}\n";

    shader_program_t program;
    char log_buffer[128] = {0};
    shader_program_status_t status = shader_program_create(&program, &g_loader, vs_src, fs_src,
                                                            log_buffer, sizeof(log_buffer));
    ASSERT_INT_EQ(SHADER_PROGRAM_ERR_COMPILE, status);
    ASSERT_TRUE(log_buffer[sizeof(log_buffer) - 1] == '\0');
    return 0;
}

static int test_shader_link_failure_reports_log(void) {
    const char *vs_src =
        "#version 330 core\n"
        "out vec3 v_color;\n"
        "void main() {\n"
        "    v_color = vec3(1.0, 0.0, 0.0);\n"
        "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0);\n"
        "}\n";
    const char *fs_src =
        "#version 330 core\n"
        "in vec2 v_color;\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = vec4(v_color, 0.0, 1.0);\n"
        "}\n";

    shader_program_t program;
    char log_buffer[256] = {0};
    shader_program_status_t status = shader_program_create(&program, &g_loader, vs_src, fs_src,
                                                            log_buffer, sizeof(log_buffer));
    ASSERT_INT_EQ(SHADER_PROGRAM_ERR_LINK, status);
    ASSERT_TRUE(log_buffer[sizeof(log_buffer) - 1] == '\0');
    return 0;
}

static int test_shader_log_truncation_is_safe(void) {
    const char *vs_src =
        "#version 330 core\n"
        "void main() {\n"
        "    gl_Position = vec4(0.0, 0.0, 0.0, 1.0\n"
        "}\n";
    const char *fs_src =
        "#version 330 core\n"
        "out vec4 out_color;\n"
        "void main() {\n"
        "    out_color = vec4(1.0);\n"
        "}\n";

    shader_program_t program;
    char log_buffer[8] = {0};
    shader_program_status_t status = shader_program_create(&program, &g_loader, vs_src, fs_src,
                                                            log_buffer, sizeof(log_buffer));
    ASSERT_INT_EQ(SHADER_PROGRAM_ERR_COMPILE, status);
    ASSERT_TRUE(log_buffer[sizeof(log_buffer) - 1] == '\0');
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"shader_compile_link_success", test_shader_compile_link_success},
    {"shader_compile_failure_reports_log", test_shader_compile_failure_reports_log},
    {"shader_link_failure_reports_log", test_shader_link_failure_reports_log},
    {"shader_log_truncation_is_safe", test_shader_log_truncation_is_safe}
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
