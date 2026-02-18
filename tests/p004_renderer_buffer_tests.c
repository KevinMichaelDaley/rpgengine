#include <glad/glad.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/vbo.h"
#include "ferrum/renderer/vao.h"
#include "ferrum/renderer/vao_attribute.h"
#include "ferrum/renderer/gl_constants.h"


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

    ctx->window = SDL_CreateWindow("p004_buffer_tests",
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

static int test_vbo_vao_create_destroy_pairs(void) {
    vbo_t vbo;
    vao_t vao;

    ASSERT_INT_EQ(VBO_OK, vbo_create(&vbo, &g_loader));
    ASSERT_TRUE(vbo_handle(&vbo) != 0u);

    ASSERT_INT_EQ(VAO_OK, vao_create(&vao, &g_loader));
    ASSERT_TRUE(vao_handle(&vao) != 0u);

    vbo_destroy(&vbo);
    vao_destroy(&vao);

    ASSERT_TRUE(vbo_handle(&vbo) == 0u);
    ASSERT_TRUE(vao_handle(&vao) == 0u);
    return 0;
}

static int test_attribute_layout_binding(void) {
    struct vertex {
        float position[3];
        int bone_index;
    };

    struct vertex vertices[3] = {
        {{0.0f, 0.0f, 0.0f}, 1},
        {{1.0f, 0.0f, 0.0f}, 2},
        {{0.0f, 1.0f, 0.0f}, 3}
    };

    vbo_t vbo;
    vao_t vao;
    ASSERT_INT_EQ(VBO_OK, vbo_create(&vbo, &g_loader));
    ASSERT_INT_EQ(VBO_OK, vbo_upload(&vbo, GL_ARRAY_BUFFER, vertices, sizeof(vertices), GL_STATIC_DRAW));

    ASSERT_INT_EQ(VAO_OK, vao_create(&vao, &g_loader));

    vao_attribute_t attrs[2];
    attrs[0] = (vao_attribute_t){0u, 3, GL_FLOAT, 0u, 0u, 0u};
    attrs[1] = (vao_attribute_t){1u, 1, GL_INT, 0u, (uint32_t)offsetof(struct vertex, bone_index), 1u};

    ASSERT_INT_EQ(VAO_OK, vao_bind_attributes(&vao, &vbo, attrs, 2u, sizeof(struct vertex)));

    glBindVertexArray(vao_handle(&vao));

    GLint size = 0;
    GLint stride = 0;
    GLint type = 0;
    GLint normalized = 0;
    GLint integer = 0;
    void *pointer = NULL;

    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_NORMALIZED, &normalized);
    glGetVertexAttribiv(0, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer);
    glGetVertexAttribPointerv(0, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);

    ASSERT_INT_EQ(3, size);
    ASSERT_INT_EQ((int)sizeof(struct vertex), stride);
    ASSERT_INT_EQ(GL_FLOAT, type);
    ASSERT_INT_EQ(GL_FALSE, normalized);
    ASSERT_INT_EQ(GL_FALSE, integer);
    ASSERT_TRUE((uintptr_t)pointer == 0u);

    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_SIZE, &size);
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_STRIDE, &stride);
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_TYPE, &type);
    glGetVertexAttribiv(1, GL_VERTEX_ATTRIB_ARRAY_INTEGER, &integer);
    glGetVertexAttribPointerv(1, GL_VERTEX_ATTRIB_ARRAY_POINTER, &pointer);

    ASSERT_INT_EQ(1, size);
    ASSERT_INT_EQ((int)sizeof(struct vertex), stride);
    ASSERT_INT_EQ(GL_INT, type);
    ASSERT_INT_EQ(GL_TRUE, integer);
    ASSERT_TRUE((uintptr_t)pointer == (uintptr_t)offsetof(struct vertex, bone_index));

    vbo_destroy(&vbo);
    vao_destroy(&vao);
    return 0;
}

static int test_zero_size_upload_is_explicit(void) {
    vbo_t vbo;
    ASSERT_INT_EQ(VBO_OK, vbo_create(&vbo, &g_loader));

    uint8_t data = 0;
    ASSERT_INT_EQ(VBO_ERR_ZERO_SIZE, vbo_upload(&vbo, GL_ARRAY_BUFFER, &data, 0u, GL_STATIC_DRAW));

    vbo_destroy(&vbo);
    return 0;
}

static int test_double_destroy_is_safe(void) {
    vbo_t vbo;
    vao_t vao;

    ASSERT_INT_EQ(VBO_OK, vbo_create(&vbo, &g_loader));
    ASSERT_INT_EQ(VAO_OK, vao_create(&vao, &g_loader));

    vbo_destroy(&vbo);
    vbo_destroy(&vbo);
    vao_destroy(&vao);
    vao_destroy(&vao);

    ASSERT_TRUE(vbo_handle(&vbo) == 0u);
    ASSERT_TRUE(vao_handle(&vao) == 0u);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"vbo_vao_create_destroy_pairs", test_vbo_vao_create_destroy_pairs},
    {"attribute_layout_binding", test_attribute_layout_binding},
    {"zero_size_upload_is_explicit", test_zero_size_upload_is_explicit},
    {"double_destroy_is_safe", test_double_destroy_is_safe}
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
