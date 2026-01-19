#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/render_pipeline_graph.h"

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

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct gl_test_context {
    SDL_Window *window;
    SDL_GLContext context;
};

static int gl_test_context_init(struct gl_test_context *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    ctx->window = SDL_CreateWindow("p004_pipeline_graph_tests",
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

struct trace_ctx {
    char *buffer;
    size_t *cursor;
    char token;
};

static void record_begin(void *user_data) {
    struct trace_ctx *ctx = (struct trace_ctx *)user_data;
    ctx->buffer[(*ctx->cursor)++] = ctx->token;
}

static void record_submit(void *user_data) {
    struct trace_ctx *ctx = (struct trace_ctx *)user_data;
    ctx->buffer[(*ctx->cursor)++] = ctx->token;
}

static void record_end(void *user_data) {
    struct trace_ctx *ctx = (struct trace_ctx *)user_data;
    ctx->buffer[(*ctx->cursor)++] = ctx->token;
}

static int run_graph(int depth_enabled, const char *expected, size_t expected_count) {
    char trace[16] = {0};
    size_t cursor = 0;

    struct trace_ctx skybox_ctx = {trace, &cursor, 'S'};
    struct trace_ctx depth_ctx = {trace, &cursor, 'D'};
    struct trace_ctx forward_ctx = {trace, &cursor, 'F'};
    struct trace_ctx post_ctx = {trace, &cursor, 'P'};

    render_pass_t skybox = {"skybox", record_begin, record_submit, record_end, &skybox_ctx};
    render_pass_t depth = {"depth_pre", record_begin, record_submit, record_end, &depth_ctx};
    render_pass_t forward = {"forward", record_begin, record_submit, record_end, &forward_ctx};
    render_pass_t post = {"post", record_begin, record_submit, record_end, &post_ctx};

    const char *forward_deps[] = {"depth_pre"};
    const char *post_deps[] = {"forward"};

    render_pipeline_graph_node_t nodes[] = {
        {&skybox, NULL, 0u, 0u},
        {&depth, NULL, 0u, RENDER_PIPELINE_NODE_FLAG_DEPTH_PREPASS},
        {&forward, forward_deps, ARRAY_SIZE(forward_deps), 0u},
        {&post, post_deps, ARRAY_SIZE(post_deps), 0u}
    };
    render_pipeline_graph_t graph = {nodes, ARRAY_SIZE(nodes)};

    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_graph_execute(&graph, depth_enabled));
    ASSERT_TRUE(cursor == expected_count);
    ASSERT_TRUE(memcmp(trace, expected, expected_count) == 0);
    return 0;
}

static int test_graph_order_with_depth_prepass(void) {
    const char expected[] = {'S', 'S', 'S', 'D', 'D', 'D', 'F', 'F', 'F', 'P', 'P', 'P'};
    return run_graph(1, expected, ARRAY_SIZE(expected));
}

static int test_graph_order_without_depth_prepass(void) {
    const char expected[] = {'S', 'S', 'S', 'F', 'F', 'F', 'P', 'P', 'P'};
    return run_graph(0, expected, ARRAY_SIZE(expected));
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"graph_order_with_depth_prepass", test_graph_order_with_depth_prepass},
    {"graph_order_without_depth_prepass", test_graph_order_without_depth_prepass}
};

int main(void) {
    struct gl_test_context ctx = {0};
    if (gl_test_context_init(&ctx) != 0) {
        return 1;
    }

    size_t total = ARRAY_SIZE(TESTS);
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
