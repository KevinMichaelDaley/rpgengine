/**
 * @file edit_dispatch_tests.c
 * @brief Unit tests for the command dispatch framework.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/edit_dispatch.h"
#include "ferrum/editor/json_parse.h"

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

#define ASSERT_UINT_EQ(a, b)                                                 \
    do {                                                                     \
        unsigned _a = (unsigned)(a), _b = (unsigned)(b);                     \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_UINT_EQ failed: %u != %u (%s:%d)\n",   \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_NOT_NULL(p)                                                   \
    do {                                                                     \
        if ((p) == NULL) {                                                   \
            fprintf(stderr, "  ASSERT_NOT_NULL failed (%s:%d)\n",            \
                    __FILE__, __LINE__);                                      \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_NULL(p)                                                       \
    do {                                                                     \
        if ((p) != NULL) {                                                   \
            fprintf(stderr, "  ASSERT_NULL failed (%s:%d)\n",                \
                    __FILE__, __LINE__);                                      \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* ----------------------------------------------------------------------- */
/* Test handlers                                                             */
/* ----------------------------------------------------------------------- */

static int handler_call_count = 0;

static bool ping_handler(edit_dispatch_t *dispatch,
                          const json_value_t *args,
                          json_value_t *result,
                          json_arena_t *arena) {
    (void)dispatch; (void)args; (void)arena;
    handler_call_count++;
    *result = (json_value_t){.type = JSON_STRING};
    result->string.ptr = "pong";
    result->string.len = 4;
    return true;
}

static bool echo_handler(edit_dispatch_t *dispatch,
                          const json_value_t *args,
                          json_value_t *result,
                          json_arena_t *arena) {
    (void)dispatch; (void)arena;
    handler_call_count++;
    if (args) {
        *result = *args;
    } else {
        *result = (json_value_t){.type = JSON_NULL};
    }
    return true;
}

static bool fail_handler(edit_dispatch_t *dispatch,
                          const json_value_t *args,
                          json_value_t *result,
                          json_arena_t *arena) {
    (void)dispatch; (void)args; (void)result; (void)arena;
    handler_call_count++;
    return false;
}

/* ----------------------------------------------------------------------- */
/* Tests                                                                     */
/* ----------------------------------------------------------------------- */

static int test_init_destroy(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));
    ASSERT_UINT_EQ(d.handler_count, 0);
    edit_dispatch_destroy(&d);
    return 0;
}

static int test_register_and_lookup(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));

    ASSERT_TRUE(edit_dispatch_register(&d, "ping", ping_handler));
    ASSERT_TRUE(edit_dispatch_register(&d, "echo", echo_handler));
    ASSERT_UINT_EQ(d.handler_count, 2);

    edit_cmd_handler_fn fn = edit_dispatch_lookup(&d, "ping", 4);
    ASSERT_TRUE(fn == ping_handler);

    fn = edit_dispatch_lookup(&d, "echo", 4);
    ASSERT_TRUE(fn == echo_handler);

    fn = edit_dispatch_lookup(&d, "nope", 4);
    ASSERT_NULL(fn);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_exec_ping(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));
    ASSERT_TRUE(edit_dispatch_register(&d, "ping", ping_handler));

    handler_call_count = 0;
    const char *cmd = "{\"id\":1,\"cmd\":\"ping\"}";
    char resp[512];
    uint32_t len = edit_dispatch_exec(&d, cmd, (uint32_t)strlen(cmd),
                                       resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_UINT_EQ(handler_call_count, 1);

    /* Response should contain id and ok. */
    ASSERT_TRUE(strstr(resp, "\"id\":1") != NULL);
    ASSERT_TRUE(strstr(resp, "\"ok\":true") != NULL);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_exec_unknown_command(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));

    const char *cmd = "{\"id\":2,\"cmd\":\"nonexistent\"}";
    char resp[512];
    uint32_t len = edit_dispatch_exec(&d, cmd, (uint32_t)strlen(cmd),
                                       resp, sizeof(resp));
    ASSERT_TRUE(len > 0);

    /* Should have error. */
    ASSERT_TRUE(strstr(resp, "\"ok\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "unknown_command") != NULL);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_exec_handler_failure(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));
    ASSERT_TRUE(edit_dispatch_register(&d, "fail", fail_handler));

    handler_call_count = 0;
    const char *cmd = "{\"id\":3,\"cmd\":\"fail\"}";
    char resp[512];
    uint32_t len = edit_dispatch_exec(&d, cmd, (uint32_t)strlen(cmd),
                                       resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_UINT_EQ(handler_call_count, 1);

    /* Handler returned false → ok:false. */
    ASSERT_TRUE(strstr(resp, "\"ok\":false") != NULL);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_exec_malformed_json(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));

    const char *cmd = "not valid json {{{";
    char resp[512];
    uint32_t len = edit_dispatch_exec(&d, cmd, (uint32_t)strlen(cmd),
                                       resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"ok\":false") != NULL);
    ASSERT_TRUE(strstr(resp, "parse_error") != NULL);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_exec_missing_cmd_field(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));

    const char *cmd = "{\"id\":4}";
    char resp[512];
    uint32_t len = edit_dispatch_exec(&d, cmd, (uint32_t)strlen(cmd),
                                       resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_TRUE(strstr(resp, "\"ok\":false") != NULL);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_exec_with_args(void) {
    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));
    ASSERT_TRUE(edit_dispatch_register(&d, "echo", echo_handler));

    handler_call_count = 0;
    const char *cmd = "{\"id\":5,\"cmd\":\"echo\",\"args\":{\"msg\":\"hello\"}}";
    char resp[1024];
    uint32_t len = edit_dispatch_exec(&d, cmd, (uint32_t)strlen(cmd),
                                       resp, sizeof(resp));
    ASSERT_TRUE(len > 0);
    ASSERT_UINT_EQ(handler_call_count, 1);
    ASSERT_TRUE(strstr(resp, "\"ok\":true") != NULL);
    /* The echo handler returns args as result; response should contain msg. */
    ASSERT_TRUE(strstr(resp, "\"msg\"") != NULL);

    edit_dispatch_destroy(&d);
    return 0;
}

static int test_null_params(void) {
    ASSERT_FALSE(edit_dispatch_init(NULL, 4096, NULL));

    edit_dispatch_t d;
    ASSERT_TRUE(edit_dispatch_init(&d, 4096, NULL));
    ASSERT_FALSE(edit_dispatch_register(&d, NULL, ping_handler));
    ASSERT_FALSE(edit_dispatch_register(&d, "x", NULL));
    ASSERT_UINT_EQ(edit_dispatch_exec(&d, NULL, 0, NULL, 0), 0);
    edit_dispatch_destroy(&d);
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
    {"init_destroy",          test_init_destroy},
    {"register_and_lookup",   test_register_and_lookup},
    {"exec_ping",             test_exec_ping},
    {"exec_unknown_command",  test_exec_unknown_command},
    {"exec_handler_failure",  test_exec_handler_failure},
    {"exec_malformed_json",   test_exec_malformed_json},
    {"exec_missing_cmd",      test_exec_missing_cmd_field},
    {"exec_with_args",        test_exec_with_args},
    {"null_params",           test_null_params},
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
