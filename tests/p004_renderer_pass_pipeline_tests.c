/**
 * @file p004_renderer_pass_pipeline_tests.c
 * @brief Tests for extended render pipeline: 9-pass architecture with
 *        per-pass draw lists and configurable capacity.
 *
 * Pure CPU tests — no GL context required.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/renderer/render_pass_type.h"
#include "ferrum/renderer/render_pipeline.h"
#include "ferrum/renderer/draw/draw_list.h"

/* ── Test macros ──────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { \
        fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_FALSE(cond) do { \
    if ((cond)) { \
        fprintf(stderr, "ASSERT_FALSE failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #cond); \
        return 1; \
    } \
} while (0)

#define ASSERT_INT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got %d\n", \
                __FILE__, __LINE__, (int)(exp), (int)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_UINT_EQ(exp, act) do { \
    if ((exp) != (act)) { \
        fprintf(stderr, "ASSERT_UINT_EQ failed: %s:%d: expected %u got %u\n", \
                __FILE__, __LINE__, (unsigned)(exp), (unsigned)(act)); \
        return 1; \
    } \
} while (0)

#define ASSERT_PTR_NOT_NULL(p) do { \
    if ((p) == NULL) { \
        fprintf(stderr, "ASSERT_PTR_NOT_NULL failed: %s:%d: %s\n", \
                __FILE__, __LINE__, #p); \
        return 1; \
    } \
} while (0)

/* ── Trace helpers ────────────────────────────────────────────────── */

#define TRACE_BUF_SIZE 128

static char g_trace[TRACE_BUF_SIZE];
static size_t g_trace_cursor;

static void trace_reset(void) {
    memset(g_trace, 0, sizeof(g_trace));
    g_trace_cursor = 0;
}

static void trace_append(char c) {
    if (g_trace_cursor < TRACE_BUF_SIZE - 1) {
        g_trace[g_trace_cursor++] = c;
    }
}

struct pass_trace {
    char begin_ch;
    char submit_ch;
    char end_ch;
};

static void trace_begin(void *ud) {
    struct pass_trace *t = (struct pass_trace *)ud;
    trace_append(t->begin_ch);
}

static void trace_submit(void *ud) {
    struct pass_trace *t = (struct pass_trace *)ud;
    trace_append(t->submit_ch);
}

static void trace_end(void *ud) {
    struct pass_trace *t = (struct pass_trace *)ud;
    trace_append(t->end_ch);
}

/* ═══════════════════════════════════════════════════════════════════
 *  PASS TYPE ENUM TESTS
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: pass type enum has 9 values ──────────────────────────── */
static int test_pass_type_count(void) {
    ASSERT_INT_EQ(9, RENDER_PASS_TYPE_COUNT);
    return 0;
}

/* ── Test: pass types have correct deterministic order ──────────── */
static int test_pass_type_order(void) {
    ASSERT_TRUE(RENDER_PASS_SHADOW   < RENDER_PASS_DEPTH_PRE);
    ASSERT_TRUE(RENDER_PASS_DEPTH_PRE < RENDER_PASS_CASTER);
    ASSERT_TRUE(RENDER_PASS_CASTER   < RENDER_PASS_LIGHT_CULL);
    ASSERT_TRUE(RENDER_PASS_LIGHT_CULL < RENDER_PASS_FORWARD);
    ASSERT_TRUE(RENDER_PASS_FORWARD  < RENDER_PASS_SKYBOX);
    ASSERT_TRUE(RENDER_PASS_SKYBOX   < RENDER_PASS_DEBUG);
    ASSERT_TRUE(RENDER_PASS_DEBUG    < RENDER_PASS_POST);
    ASSERT_TRUE(RENDER_PASS_POST     < RENDER_PASS_UI);
    return 0;
}

/* ── Test: pass type name strings ───────────────────────────────── */
static int test_pass_type_names(void) {
    ASSERT_TRUE(render_pass_type_name(RENDER_PASS_SHADOW) != NULL);
    ASSERT_TRUE(render_pass_type_name(RENDER_PASS_FORWARD) != NULL);
    ASSERT_TRUE(render_pass_type_name(RENDER_PASS_UI) != NULL);
    ASSERT_TRUE(strcmp(render_pass_type_name(RENDER_PASS_SHADOW), "shadow") == 0);
    ASSERT_TRUE(strcmp(render_pass_type_name(RENDER_PASS_FORWARD), "forward") == 0);
    ASSERT_TRUE(strcmp(render_pass_type_name(RENDER_PASS_UI), "ui") == 0);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  PIPELINE INIT / DESTROY
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init allocates passes and draw lists ─────────────────── */
static int test_pipeline_init(void) {
    render_pipeline_t pipeline;
    int rc = render_pipeline_init(&pipeline, 256);
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, rc);
    ASSERT_UINT_EQ(RENDER_PASS_TYPE_COUNT, (unsigned)pipeline.pass_count);

    /* Each pass should have a name and a draw list. */
    for (size_t i = 0; i < pipeline.pass_count; ++i) {
        ASSERT_PTR_NOT_NULL(pipeline.passes[i].name);
        ASSERT_PTR_NOT_NULL(pipeline.passes[i].draw_list);
        ASSERT_UINT_EQ(0, pipeline.passes[i].draw_list->count);
        ASSERT_UINT_EQ(256, pipeline.passes[i].draw_list->capacity);
    }

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ── Test: init with large capacity ─────────────────────────────── */
static int test_pipeline_init_large(void) {
    render_pipeline_t pipeline;
    int rc = render_pipeline_init(&pipeline, 16384);
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, rc);
    ASSERT_UINT_EQ(16384, pipeline.passes[0].draw_list->capacity);
    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ── Test: destroy NULL is safe ─────────────────────────────────── */
static int test_pipeline_destroy_null(void) {
    render_pipeline_destroy(NULL);
    return 0;
}

/* ── Test: pass order matches enum order after init ─────────────── */
static int test_pipeline_pass_order(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    for (size_t i = 0; i < pipeline.pass_count; ++i) {
        ASSERT_INT_EQ((int)i, (int)pipeline.passes[i].pass_type);
    }

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  PIPELINE EXECUTE
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: execute runs all 9 passes in order ───────────────────── */
static int test_pipeline_execute_order(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    trace_reset();

    /* Assign trace callbacks to shadow(S), forward(F), ui(U). */
    struct pass_trace shadow_t = {'S', 's', '$'};
    struct pass_trace fwd_t    = {'F', 'f', '%'};
    struct pass_trace ui_t     = {'U', 'u', '!'};

    pipeline.passes[RENDER_PASS_SHADOW].begin    = trace_begin;
    pipeline.passes[RENDER_PASS_SHADOW].submit   = trace_submit;
    pipeline.passes[RENDER_PASS_SHADOW].end      = trace_end;
    pipeline.passes[RENDER_PASS_SHADOW].user_data = &shadow_t;

    pipeline.passes[RENDER_PASS_FORWARD].begin    = trace_begin;
    pipeline.passes[RENDER_PASS_FORWARD].submit   = trace_submit;
    pipeline.passes[RENDER_PASS_FORWARD].end      = trace_end;
    pipeline.passes[RENDER_PASS_FORWARD].user_data = &fwd_t;

    pipeline.passes[RENDER_PASS_UI].begin    = trace_begin;
    pipeline.passes[RENDER_PASS_UI].submit   = trace_submit;
    pipeline.passes[RENDER_PASS_UI].end      = trace_end;
    pipeline.passes[RENDER_PASS_UI].user_data = &ui_t;

    int rc = render_pipeline_execute(&pipeline);
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, rc);

    /* Shadow must execute before forward, forward before UI.
     * Find their positions in the trace. */
    const char *s_pos = strchr(g_trace, 'S');
    const char *f_pos = strchr(g_trace, 'F');
    const char *u_pos = strchr(g_trace, 'U');
    ASSERT_PTR_NOT_NULL(s_pos);
    ASSERT_PTR_NOT_NULL(f_pos);
    ASSERT_PTR_NOT_NULL(u_pos);
    ASSERT_TRUE(s_pos < f_pos);
    ASSERT_TRUE(f_pos < u_pos);

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ── Test: passes with NULL callbacks are skipped gracefully ────── */
static int test_pipeline_null_callbacks(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    /* All callbacks are NULL by default — execute should not crash. */
    int rc = render_pipeline_execute(&pipeline);
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, rc);

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ── Test: get_pass returns correct pass by type ────────────────── */
static int test_get_pass(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    render_pass_t *forward = render_pipeline_get_pass(&pipeline, RENDER_PASS_FORWARD);
    ASSERT_PTR_NOT_NULL(forward);
    ASSERT_INT_EQ(RENDER_PASS_FORWARD, (int)forward->pass_type);
    ASSERT_TRUE(strcmp(forward->name, "forward") == 0);

    render_pass_t *debug = render_pipeline_get_pass(&pipeline, RENDER_PASS_DEBUG);
    ASSERT_PTR_NOT_NULL(debug);
    ASSERT_TRUE(strcmp(debug->name, "debug") == 0);

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ── Test: draw list push through pipeline pass ─────────────────── */
static int test_pipeline_draw_list_push(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    render_pass_t *forward = render_pipeline_get_pass(&pipeline, RENDER_PASS_FORWARD);
    draw_command_t cmd = {0};
    cmd.sort_key = draw_sort_key_build(1, 2, 3, 4);
    cmd.instance_count = 1;

    int rc = draw_list_push(forward->draw_list, &cmd);
    ASSERT_INT_EQ(DRAW_LIST_OK, rc);
    ASSERT_UINT_EQ(1, forward->draw_list->count);

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ── Test: clear_all_draw_lists resets all passes ───────────────── */
static int test_pipeline_clear_all(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    /* Push a command into each pass. */
    draw_command_t cmd = {0};
    cmd.instance_count = 1;
    for (size_t i = 0; i < pipeline.pass_count; ++i) {
        draw_list_push(pipeline.passes[i].draw_list, &cmd);
    }

    render_pipeline_clear_draw_lists(&pipeline);

    for (size_t i = 0; i < pipeline.pass_count; ++i) {
        ASSERT_UINT_EQ(0, pipeline.passes[i].draw_list->count);
    }

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  BACKWARD COMPAT
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: old render_pipeline_default still works ──────────────── */
static int test_backward_compat_default(void) {
    trace_reset();
    struct pass_trace sky_t = {'S', 's', '$'};
    struct pass_trace fwd_t = {'F', 'f', '%'};
    struct pass_trace pst_t = {'P', 'p', '!'};

    render_pass_t skybox  = {"skybox",  trace_begin, trace_submit, trace_end, &sky_t};
    render_pass_t forward = {"forward", trace_begin, trace_submit, trace_end, &fwd_t};
    render_pass_t post    = {"post",    trace_begin, trace_submit, trace_end, &pst_t};

    render_pipeline_t pipeline;
    render_pass_t storage[3];
    int rc = render_pipeline_default(&pipeline, storage, &skybox, &forward, &post);
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, rc);

    rc = render_pipeline_execute(&pipeline);
    ASSERT_INT_EQ(RENDER_PIPELINE_OK, rc);

    /* Should still execute skybox→forward→post. */
    ASSERT_TRUE(g_trace_cursor >= 9);
    ASSERT_TRUE(g_trace[0] == 'S');
    ASSERT_TRUE(g_trace[3] == 'F');
    ASSERT_TRUE(g_trace[6] == 'P');

    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  FAILURE MODES
 * ═══════════════════════════════════════════════════════════════════ */

/* ── Test: init NULL pipeline ───────────────────────────────────── */
static int test_init_null(void) {
    int rc = render_pipeline_init(NULL, 64);
    ASSERT_INT_EQ(RENDER_PIPELINE_ERR_INVALID, rc);
    return 0;
}

/* ── Test: init zero capacity ───────────────────────────────────── */
static int test_init_zero_capacity(void) {
    render_pipeline_t pipeline;
    int rc = render_pipeline_init(&pipeline, 0);
    ASSERT_INT_EQ(RENDER_PIPELINE_ERR_INVALID, rc);
    return 0;
}

/* ── Test: get_pass with out-of-range type ──────────────────────── */
static int test_get_pass_invalid(void) {
    render_pipeline_t pipeline;
    render_pipeline_init(&pipeline, 64);

    render_pass_t *p = render_pipeline_get_pass(&pipeline, RENDER_PASS_TYPE_COUNT);
    ASSERT_TRUE(p == NULL);

    render_pipeline_destroy(&pipeline);
    return 0;
}

/* ═══════════════════════════════════════════════════════════════════
 *  Test runner
 * ═══════════════════════════════════════════════════════════════════ */

typedef struct { const char *name; int (*fn)(void); } test_entry_t;

static const test_entry_t TESTS[] = {
    /* Pass type enum */
    {"pass_type_count",          test_pass_type_count},
    {"pass_type_order",          test_pass_type_order},
    {"pass_type_names",          test_pass_type_names},
    /* Pipeline init/destroy */
    {"pipeline_init",            test_pipeline_init},
    {"pipeline_init_large",      test_pipeline_init_large},
    {"pipeline_destroy_null",    test_pipeline_destroy_null},
    {"pipeline_pass_order",      test_pipeline_pass_order},
    /* Pipeline execute */
    {"pipeline_execute_order",   test_pipeline_execute_order},
    {"pipeline_null_callbacks",  test_pipeline_null_callbacks},
    {"get_pass",                 test_get_pass},
    {"pipeline_draw_list_push",  test_pipeline_draw_list_push},
    {"pipeline_clear_all",       test_pipeline_clear_all},
    /* Backward compat */
    {"backward_compat_default",  test_backward_compat_default},
    /* Failure modes */
    {"init_null",                test_init_null},
    {"init_zero_capacity",       test_init_zero_capacity},
    {"get_pass_invalid",         test_get_pass_invalid},
};

int main(void) {
    size_t total = sizeof(TESTS) / sizeof(TESTS[0]);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        printf("RUN  %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc == 0) {
            printf("  OK %s\n", TESTS[i].name);
            ++passed;
        } else {
            printf("FAIL %s\n", TESTS[i].name);
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
