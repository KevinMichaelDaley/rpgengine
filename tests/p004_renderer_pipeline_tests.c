#include <stdio.h>

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

struct trace_ctx {
    char *buffer;
    size_t *cursor;
    char token;
};

static void record_begin(void *user) {
    struct trace_ctx *ctx = (struct trace_ctx *)user;
    ctx->buffer[(*ctx->cursor)++] = ctx->token;
}

static void record_submit(void *user) {
    struct trace_ctx *ctx = (struct trace_ctx *)user;
    ctx->buffer[(*ctx->cursor)++] = ctx->token;
}

static void record_end(void *user) {
    struct trace_ctx *ctx = (struct trace_ctx *)user;
    ctx->buffer[(*ctx->cursor)++] = ctx->token;
}

static int test_pipeline_execution_order(void) {
    char trace[16] = {0};
    size_t cursor = 0;

    struct trace_ctx skybox_ctx = {trace, &cursor, 'S'};
    struct trace_ctx forward_ctx = {trace, &cursor, 'F'};
    struct trace_ctx post_ctx = {trace, &cursor, 'P'};

    render_pass_t skybox = {"skybox", record_begin, record_submit, record_end, &skybox_ctx};
    render_pass_t forward = {"forward", record_begin, record_submit, record_end, &forward_ctx};
    render_pass_t post = {"post", record_begin, record_submit, record_end, &post_ctx};

    render_pipeline_t pipeline;
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_default(&pipeline, &skybox, &forward, &post));
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_execute(&pipeline));

    ASSERT_TRUE(cursor == 9u);
    ASSERT_TRUE(trace[0] == 'S' && trace[1] == 'S' && trace[2] == 'S');
    ASSERT_TRUE(trace[3] == 'F' && trace[4] == 'F' && trace[5] == 'F');
    ASSERT_TRUE(trace[6] == 'P' && trace[7] == 'P' && trace[8] == 'P');

    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"pipeline_execution_order", test_pipeline_execution_order}
};

int main(void) {
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
    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
