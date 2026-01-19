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
    render_pass_t storage[3];
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_default(&pipeline, storage,
                                                             &skybox, &forward, &post));
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_execute(&pipeline));

    ASSERT_TRUE(cursor == 9u);
    ASSERT_TRUE(trace[0] == 'S' && trace[1] == 'S' && trace[2] == 'S');
    ASSERT_TRUE(trace[3] == 'F' && trace[4] == 'F' && trace[5] == 'F');
    ASSERT_TRUE(trace[6] == 'P' && trace[7] == 'P' && trace[8] == 'P');

    return 0;
}

static int test_pipeline_storage_isolation(void) {
    char trace_a[16] = {0};
    char trace_b[16] = {0};
    size_t cursor_a = 0;
    size_t cursor_b = 0;

    struct trace_ctx skybox_ctx_a = {trace_a, &cursor_a, 'A'};
    struct trace_ctx forward_ctx_a = {trace_a, &cursor_a, 'B'};
    struct trace_ctx post_ctx_a = {trace_a, &cursor_a, 'C'};

    struct trace_ctx skybox_ctx_b = {trace_b, &cursor_b, 'X'};
    struct trace_ctx forward_ctx_b = {trace_b, &cursor_b, 'Y'};
    struct trace_ctx post_ctx_b = {trace_b, &cursor_b, 'Z'};

    render_pass_t skybox_a = {"skybox_a", record_begin, record_submit, record_end, &skybox_ctx_a};
    render_pass_t forward_a = {"forward_a", record_begin, record_submit, record_end, &forward_ctx_a};
    render_pass_t post_a = {"post_a", record_begin, record_submit, record_end, &post_ctx_a};

    render_pass_t skybox_b = {"skybox_b", record_begin, record_submit, record_end, &skybox_ctx_b};
    render_pass_t forward_b = {"forward_b", record_begin, record_submit, record_end, &forward_ctx_b};
    render_pass_t post_b = {"post_b", record_begin, record_submit, record_end, &post_ctx_b};

    render_pipeline_t pipeline_a;
    render_pipeline_t pipeline_b;
    render_pass_t storage_a[3];
    render_pass_t storage_b[3];
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_default(&pipeline_a, storage_a,
                                                             &skybox_a, &forward_a, &post_a));
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_default(&pipeline_b, storage_b,
                                                             &skybox_b, &forward_b, &post_b));
    ASSERT_TRUE(pipeline_a.passes != pipeline_b.passes);

    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_execute(&pipeline_a));
    ASSERT_TRUE(cursor_a == 9u);
    ASSERT_TRUE(trace_a[0] == 'A' && trace_a[1] == 'A' && trace_a[2] == 'A');
    ASSERT_TRUE(trace_a[3] == 'B' && trace_a[4] == 'B' && trace_a[5] == 'B');
    ASSERT_TRUE(trace_a[6] == 'C' && trace_a[7] == 'C' && trace_a[8] == 'C');

    ASSERT_INT_EQ(RENDER_PIPELINE_OK, render_pipeline_execute(&pipeline_b));
    ASSERT_TRUE(cursor_b == 9u);
    ASSERT_TRUE(trace_b[0] == 'X' && trace_b[1] == 'X' && trace_b[2] == 'X');
    ASSERT_TRUE(trace_b[3] == 'Y' && trace_b[4] == 'Y' && trace_b[5] == 'Y');
    ASSERT_TRUE(trace_b[6] == 'Z' && trace_b[7] == 'Z' && trace_b[8] == 'Z');

    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"pipeline_execution_order", test_pipeline_execution_order},
    {"pipeline_storage_isolation", test_pipeline_storage_isolation}
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
