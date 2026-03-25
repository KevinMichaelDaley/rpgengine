/**
 * @file inline_field_tests.c
 * @brief Unit tests for the inline field editor widget.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/ui/inline_field.h"

/* ----------------------------------------------------------------------- */
/* Test harness                                                             */
/* ----------------------------------------------------------------------- */

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            fprintf(stderr, "  ASSERT_TRUE failed: %s (%s:%d)\n",            \
                    #expr, __FILE__, __LINE__);                               \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FALSE(expr) ASSERT_TRUE(!(expr))

#define ASSERT_FLOAT_NEAR(a, b, eps)                                         \
    do {                                                                     \
        float _a = (float)(a), _b = (float)(b);                             \
        float _d = (_a > _b) ? (_a - _b) : (_b - _a);                      \
        if (_d > (eps)) {                                                    \
            fprintf(stderr, "  ASSERT_FLOAT_NEAR: %f != %f (%s:%d)\n",      \
                    (double)_a, (double)_b, __FILE__, __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                  \
    do {                                                                     \
        if (strcmp((a), (b)) != 0) {                                         \
            fprintf(stderr, "  ASSERT_STR_EQ: '%s' != '%s' (%s:%d)\n",      \
                    (a), (b), __FILE__, __LINE__);                            \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Test: begin sets buffer from value                                        */
/* ----------------------------------------------------------------------- */

static int test_begin_sets_buffer(void) {
    inline_field_ctx_t ctx;
    inline_field_state_t field;
    memset(&ctx, 0, sizeof(ctx));
    memset(&field, 0, sizeof(field));

    inline_field_begin(&ctx, &field, 42, 3.14f);
    ASSERT_TRUE(field.active);
    ASSERT_STR_EQ(field.buf, "3.14");
    ASSERT_FLOAT_NEAR(field.original_value, 3.14f, 0.001f);
    ASSERT_TRUE(ctx.active_field == &field);

    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: commit parses float                                                 */
/* ----------------------------------------------------------------------- */

static int test_commit_parses_float(void) {
    inline_field_ctx_t ctx;
    inline_field_state_t field;
    memset(&ctx, 0, sizeof(ctx));
    memset(&field, 0, sizeof(field));

    inline_field_begin(&ctx, &field, 1, 0.0f);
    /* Overwrite buffer. */
    strncpy(field.buf, "2.5", sizeof(field.buf));
    field.cursor = 3;

    float out = 0.0f;
    ASSERT_TRUE(inline_field_commit(&ctx, &out));
    ASSERT_FLOAT_NEAR(out, 2.5f, 0.001f);
    ASSERT_FALSE(field.active);
    ASSERT_TRUE(ctx.active_field == NULL);

    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: cancel restores original                                            */
/* ----------------------------------------------------------------------- */

static int test_cancel_restores_original(void) {
    inline_field_ctx_t ctx;
    inline_field_state_t field;
    memset(&ctx, 0, sizeof(ctx));
    memset(&field, 0, sizeof(field));

    inline_field_begin(&ctx, &field, 1, 7.77f);
    strncpy(field.buf, "999", sizeof(field.buf));

    float out = 0.0f;
    inline_field_cancel(&ctx, &out);
    ASSERT_FLOAT_NEAR(out, 7.77f, 0.001f);
    ASSERT_FALSE(field.active);

    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: handle_text appends characters                                      */
/* ----------------------------------------------------------------------- */

static int test_handle_text(void) {
    inline_field_ctx_t ctx;
    inline_field_state_t field;
    memset(&ctx, 0, sizeof(ctx));
    memset(&field, 0, sizeof(field));

    inline_field_begin(&ctx, &field, 1, 0.0f);
    /* Clear buffer to start fresh. */
    field.buf[0] = '\0';
    field.cursor = 0;

    inline_field_handle_text(&ctx, '1');
    inline_field_handle_text(&ctx, '.');
    inline_field_handle_text(&ctx, '5');

    ASSERT_STR_EQ(field.buf, "1.5");
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: handle_key backspace                                                */
/* ----------------------------------------------------------------------- */

static int test_handle_key_backspace(void) {
    inline_field_ctx_t ctx;
    inline_field_state_t field;
    memset(&ctx, 0, sizeof(ctx));
    memset(&field, 0, sizeof(field));

    inline_field_begin(&ctx, &field, 1, 0.0f);
    field.buf[0] = '1'; field.buf[1] = '2'; field.buf[2] = '3';
    field.buf[3] = '\0';
    field.cursor = 3;

    inline_field_handle_key(&ctx, INLINE_FIELD_KEY_BACKSPACE);
    ASSERT_STR_EQ(field.buf, "12");

    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: negative values                                                     */
/* ----------------------------------------------------------------------- */

static int test_negative_value(void) {
    inline_field_ctx_t ctx;
    inline_field_state_t field;
    memset(&ctx, 0, sizeof(ctx));
    memset(&field, 0, sizeof(field));

    inline_field_begin(&ctx, &field, 1, -1.5f);
    /* Buffer should start with negative. */
    ASSERT_TRUE(field.buf[0] == '-');

    float out = 0.0f;
    ASSERT_TRUE(inline_field_commit(&ctx, &out));
    ASSERT_FLOAT_NEAR(out, -1.5f, 0.01f);

    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test: null params                                                         */
/* ----------------------------------------------------------------------- */

static int test_null_params(void) {
    inline_field_begin(NULL, NULL, 0, 0.0f);
    inline_field_cancel(NULL, NULL);
    float out;
    ASSERT_FALSE(inline_field_commit(NULL, &out));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Test runner                                                              */
/* ----------------------------------------------------------------------- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"begin_sets_buffer",       test_begin_sets_buffer},
    {"commit_parses_float",     test_commit_parses_float},
    {"cancel_restores_original", test_cancel_restores_original},
    {"handle_text",             test_handle_text},
    {"handle_key_backspace",    test_handle_key_backspace},
    {"negative_value",          test_negative_value},
    {"null_params",             test_null_params},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("OK   %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }
    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
