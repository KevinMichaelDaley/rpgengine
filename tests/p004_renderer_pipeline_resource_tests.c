#include <GL/glew.h>
#include <SDL2/SDL.h>
#include <stdint.h>
#include <stdio.h>

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/render_pipeline.h"

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

static int gl_test_context_init(struct gl_test_context *ctx) {
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return -1;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_FORWARD_COMPATIBLE_FLAG);

    ctx->window = SDL_CreateWindow("p004_pipeline_resource_tests",
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

struct pass_ctx {
    render_resource_view_set_t view_set;
    render_resource_attachment_t attachments[2];
    render_resource_t transients[1];
    render_resource_view_t views[2];
    render_pipeline_t *pipeline;
    size_t pass_index;
    int bind_status;
};

static void pass_bind_resources(void *user_data) {
    struct pass_ctx *ctx = (struct pass_ctx *)user_data;
    ctx->bind_status = render_pipeline_bind_resources(ctx->pipeline,
                                                      ctx->pass_index,
                                                      &ctx->view_set);
}

static void pass_submit(void *user_data) {
    struct pass_ctx *ctx = (struct pass_ctx *)user_data;
    if (ctx->bind_status != RENDER_PIPELINE_OK) {
        return;
    }
    GLuint fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, (GLint *)&fbo);
    if (fbo == 0u) {
        ctx->bind_status = RENDER_PIPELINE_ERR_INVALID;
    }
}

static int test_pipeline_resource_views_bind(void) {
    uint32_t framebuffer = 0u;
    glGenFramebuffers(1, &framebuffer);

    render_resource_attachment_t color = {
        .type = RENDER_RESOURCE_ATTACHMENT_COLOR,
        .format = RENDER_RESOURCE_FORMAT_RGBA8,
        .handle = framebuffer,
        .width = 4u,
        .height = 4u
    };
    render_resource_attachment_t depth = {
        .type = RENDER_RESOURCE_ATTACHMENT_DEPTH,
        .format = RENDER_RESOURCE_FORMAT_DEPTH24_STENCIL8,
        .handle = framebuffer,
        .width = 4u,
        .height = 4u
    };
    render_resource_t transient = {
        .type = RENDER_RESOURCE_TYPE_BUFFER,
        .handle = 0u,
        .size = 256u,
        .usage = RENDER_RESOURCE_USAGE_VERTEX
    };
    render_resource_view_t views[2] = {
        {.kind = RENDER_RESOURCE_VIEW_ATTACHMENT, .index = 0u},
        {.kind = RENDER_RESOURCE_VIEW_ATTACHMENT, .index = 1u}
    };
    render_resource_view_set_t view_set = {
        .attachments = &color,
        .attachment_count = 2u,
        .transients = &transient,
        .transient_count = 1u,
        .inputs = views,
        .input_count = 2u,
        .outputs = views,
        .output_count = 2u
    };

    struct pass_ctx ctx = {0};
    ctx.view_set = view_set;
    ctx.attachments[0] = color;
    ctx.attachments[1] = depth;
    ctx.transients[0] = transient;
    ctx.views[0] = views[0];
    ctx.views[1] = views[1];
    ctx.view_set.attachments = ctx.attachments;
    ctx.view_set.transients = ctx.transients;
    ctx.view_set.inputs = ctx.views;
    ctx.view_set.outputs = ctx.views;

    render_pass_t skybox = {"skybox", NULL, NULL, NULL, NULL};
    render_pass_t forward = {"forward", pass_bind_resources, pass_submit, NULL, &ctx};
    render_pass_t post = {"post", NULL, NULL, NULL, NULL};

    render_pipeline_t pipeline;
    render_pass_t storage[3];
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_default(&pipeline, storage,
                                                             &skybox, &forward, &post));
    pipeline.glBindFramebuffer = glBindFramebuffer;
    ctx.pipeline = &pipeline;
    ctx.pass_index = 1u;
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_execute(&pipeline));
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, ctx.bind_status);

    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_unbind_resources(&pipeline, 1u));
    glDeleteFramebuffers(1, &framebuffer);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"pipeline_resource_views_bind", test_pipeline_resource_views_bind}
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
