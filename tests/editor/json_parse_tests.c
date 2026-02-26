/**
 * @file json_parse_tests.c
 * @brief Unit tests for the minimal JSON parser and serializer.
 *
 * Tests cover: happy path parsing, malformed input, nested objects, arrays of
 * numbers, string escapes, buffer overflow, arena exhaustion, serialization
 * round-trips, and accessor helpers.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "ferrum/editor/json_parse.h"

/* ----------------------------------------------------------------------- */
/* Test harness macros                                                      */
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

#define ASSERT_INT_EQ(a, b)                                                  \
    do {                                                                     \
        int _a = (int)(a), _b = (int)(b);                                    \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_INT_EQ failed: %d != %d (%s:%d)\n",    \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_UINT_EQ(a, b)                                                 \
    do {                                                                     \
        unsigned _a = (unsigned)(a), _b = (unsigned)(b);                     \
        if (_a != _b) {                                                      \
            fprintf(stderr, "  ASSERT_UINT_EQ failed: %u != %u (%s:%d)\n",   \
                    _a, _b, __FILE__, __LINE__);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_FLOAT_NEAR(a, b, eps)                                         \
    do {                                                                     \
        double _a = (double)(a), _b = (double)(b);                           \
        if (fabs(_a - _b) > (eps)) {                                         \
            fprintf(stderr,                                                  \
                    "  ASSERT_FLOAT_NEAR failed: %f != %f (eps=%f) (%s:%d)\n",\
                    _a, _b, (double)(eps), __FILE__, __LINE__);              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_STR_EQ(a, b)                                                  \
    do {                                                                     \
        if (strcmp((a), (b)) != 0) {                                         \
            fprintf(stderr, "  ASSERT_STR_EQ failed: \"%s\" != \"%s\""       \
                    " (%s:%d)\n", (a), (b), __FILE__, __LINE__);             \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_NULL(ptr) ASSERT_TRUE((ptr) == NULL)
#define ASSERT_NOT_NULL(ptr) ASSERT_TRUE((ptr) != NULL)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Shared arena buffer for tests. */
static uint8_t g_arena_buf[64 * 1024]; /* 64 KB — plenty for tests */

static json_arena_t make_arena(void) {
    json_arena_t a;
    json_arena_init(&a, g_arena_buf, sizeof(g_arena_buf));
    return a;
}

/* ----------------------------------------------------------------------- */
/* Happy-path parse tests                                                   */
/* ----------------------------------------------------------------------- */

static int test_parse_null(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("null", 4, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_NULL);
    return 0;
}

static int test_parse_true(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("true", 4, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_BOOL);
    ASSERT_TRUE(val.boolean);
    return 0;
}

static int test_parse_false(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("false", 5, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_BOOL);
    ASSERT_FALSE(val.boolean);
    return 0;
}

static int test_parse_integer(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("42", 2, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_NUMBER);
    ASSERT_FLOAT_NEAR(val.number, 42.0, 0.001);
    return 0;
}

static int test_parse_negative_number(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("-3.14", 5, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_NUMBER);
    ASSERT_FLOAT_NEAR(val.number, -3.14, 0.001);
    return 0;
}

static int test_parse_exponent(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("1.5e3", 5, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_NUMBER);
    ASSERT_FLOAT_NEAR(val.number, 1500.0, 0.1);
    return 0;
}

static int test_parse_string(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "\"hello world\"";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_STRING);
    ASSERT_UINT_EQ(val.string.len, 11);
    ASSERT_TRUE(memcmp(val.string.ptr, "hello world", 11) == 0);
    return 0;
}

static int test_parse_string_escapes(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "\"a\\nb\\tc\\\\d\\\"e\"";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_STRING);
    /* Expected: a\nb\tc\d"e  (9 chars) */
    ASSERT_UINT_EQ(val.string.len, 9);
    ASSERT_TRUE(val.string.ptr[0] == 'a');
    ASSERT_TRUE(val.string.ptr[1] == '\n');
    ASSERT_TRUE(val.string.ptr[2] == 'b');
    ASSERT_TRUE(val.string.ptr[3] == '\t');
    ASSERT_TRUE(val.string.ptr[4] == 'c');
    ASSERT_TRUE(val.string.ptr[5] == '\\');
    ASSERT_TRUE(val.string.ptr[6] == 'd');
    ASSERT_TRUE(val.string.ptr[7] == '"');
    ASSERT_TRUE(val.string.ptr[8] == 'e');
    return 0;
}

static int test_parse_empty_array(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("[]", 2, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_ARRAY);
    ASSERT_UINT_EQ(val.array.count, 0);
    return 0;
}

static int test_parse_number_array(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "[1, 2, 3.5]";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_ARRAY);
    ASSERT_UINT_EQ(val.array.count, 3);
    ASSERT_FLOAT_NEAR(val.array.items[0].number, 1.0, 0.001);
    ASSERT_FLOAT_NEAR(val.array.items[1].number, 2.0, 0.001);
    ASSERT_FLOAT_NEAR(val.array.items[2].number, 3.5, 0.001);
    return 0;
}

static int test_parse_empty_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("{}", 2, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_OBJECT);
    ASSERT_UINT_EQ(val.object.count, 0);
    return 0;
}

static int test_parse_simple_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "{\"id\": 1, \"cmd\": \"spawn\", \"ok\": true}";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_OBJECT);
    ASSERT_UINT_EQ(val.object.count, 3);

    const json_value_t *id = json_object_get(&val, "id");
    ASSERT_NOT_NULL(id);
    ASSERT_INT_EQ(id->type, JSON_NUMBER);
    ASSERT_FLOAT_NEAR(id->number, 1.0, 0.001);

    const json_value_t *cmd = json_object_get(&val, "cmd");
    ASSERT_NOT_NULL(cmd);
    ASSERT_INT_EQ(cmd->type, JSON_STRING);

    char cmd_str[32];
    ASSERT_TRUE(json_string_copy(cmd, cmd_str, sizeof(cmd_str)));
    ASSERT_STR_EQ(cmd_str, "spawn");

    const json_value_t *ok = json_object_get(&val, "ok");
    ASSERT_NOT_NULL(ok);
    ASSERT_INT_EQ(ok->type, JSON_BOOL);
    ASSERT_TRUE(ok->boolean);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Nested structure tests                                                   */
/* ----------------------------------------------------------------------- */

static int test_parse_nested_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input =
        "{\"id\":1,\"cmd\":\"spawn\","
        "\"args\":{\"type\":\"box\",\"size\":[2,2,2],\"pos\":[10,0,5]}}";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_OBJECT);

    const json_value_t *args = json_object_get(&val, "args");
    ASSERT_NOT_NULL(args);
    ASSERT_INT_EQ(args->type, JSON_OBJECT);

    const json_value_t *type = json_object_get(args, "type");
    ASSERT_NOT_NULL(type);
    char type_str[16];
    json_string_copy(type, type_str, sizeof(type_str));
    ASSERT_STR_EQ(type_str, "box");

    const json_value_t *size = json_object_get(args, "size");
    ASSERT_NOT_NULL(size);
    ASSERT_INT_EQ(size->type, JSON_ARRAY);
    ASSERT_UINT_EQ(size->array.count, 3);
    ASSERT_FLOAT_NEAR(size->array.items[0].number, 2.0, 0.001);
    ASSERT_FLOAT_NEAR(size->array.items[1].number, 2.0, 0.001);
    ASSERT_FLOAT_NEAR(size->array.items[2].number, 2.0, 0.001);

    const json_value_t *pos = json_object_get(args, "pos");
    ASSERT_NOT_NULL(pos);
    ASSERT_UINT_EQ(pos->array.count, 3);
    ASSERT_FLOAT_NEAR(pos->array.items[0].number, 10.0, 0.001);
    ASSERT_FLOAT_NEAR(pos->array.items[1].number, 0.0, 0.001);
    ASSERT_FLOAT_NEAR(pos->array.items[2].number, 5.0, 0.001);
    return 0;
}

static int test_parse_array_of_objects(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "[{\"x\":1},{\"x\":2},{\"x\":3}]";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_ARRAY);
    ASSERT_UINT_EQ(val.array.count, 3);
    for (uint32_t i = 0; i < 3; ++i) {
        ASSERT_INT_EQ(val.array.items[i].type, JSON_OBJECT);
        const json_value_t *x = json_object_get(&val.array.items[i], "x");
        ASSERT_NOT_NULL(x);
        ASSERT_FLOAT_NEAR(x->number, (double)(i + 1), 0.001);
    }
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Edge cases                                                               */
/* ----------------------------------------------------------------------- */

static int test_parse_whitespace(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "  \t\n { \n \"a\" \t : \n 1 \n } \n ";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_OBJECT);
    const json_value_t *a = json_object_get(&val, "a");
    ASSERT_NOT_NULL(a);
    ASSERT_FLOAT_NEAR(a->number, 1.0, 0.001);
    return 0;
}

static int test_parse_empty_string(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("\"\"", 2, &arena, &val));
    ASSERT_INT_EQ(val.type, JSON_STRING);
    ASSERT_UINT_EQ(val.string.len, 0);
    return 0;
}

static int test_parse_zero(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("0", 1, &arena, &val));
    ASSERT_FLOAT_NEAR(val.number, 0.0, 0.001);
    return 0;
}

static int test_parse_negative_exponent(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("5e-2", 4, &arena, &val));
    ASSERT_FLOAT_NEAR(val.number, 0.05, 0.001);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Failure mode tests                                                       */
/* ----------------------------------------------------------------------- */

static int test_parse_empty_input(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_FALSE(json_parse("", 0, &arena, &val));
    return 0;
}

static int test_parse_truncated_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_FALSE(json_parse("{\"a\":", 5, &arena, &val));
    return 0;
}

static int test_parse_truncated_array(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_FALSE(json_parse("[1, 2, ", 7, &arena, &val));
    return 0;
}

static int test_parse_invalid_token(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_FALSE(json_parse("undefined", 9, &arena, &val));
    return 0;
}

static int test_parse_trailing_comma_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    /* Trailing commas are invalid in standard JSON. */
    ASSERT_FALSE(json_parse("{\"a\":1,}", 8, &arena, &val));
    return 0;
}

static int test_parse_trailing_comma_array(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_FALSE(json_parse("[1,2,]", 6, &arena, &val));
    return 0;
}

static int test_parse_missing_colon(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_FALSE(json_parse("{\"a\" 1}", 7, &arena, &val));
    return 0;
}

static int test_parse_duplicate_key_not_rejected(void) {
    /* Standard JSON technically allows duplicate keys; we don't reject them.
     * The last value wins via json_object_get(). */
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("{\"a\":1,\"a\":2}", 13, &arena, &val));
    return 0;
}

static int test_parse_arena_exhaustion(void) {
    /* Use a tiny arena that can't fit a nested object. */
    uint8_t tiny_buf[32];
    json_arena_t arena;
    json_arena_init(&arena, tiny_buf, sizeof(tiny_buf));
    json_value_t val;
    const char *input =
        "{\"a\":{\"b\":{\"c\":[1,2,3,4,5,6,7,8,9,10]}}}";
    ASSERT_FALSE(json_parse(input, strlen(input), &arena, &val));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Accessor tests                                                           */
/* ----------------------------------------------------------------------- */

static int test_object_get_missing_key(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("{\"a\":1}", 7, &arena, &val));
    ASSERT_NULL(json_object_get(&val, "b"));
    return 0;
}

static int test_object_get_on_non_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("[1,2]", 5, &arena, &val));
    ASSERT_NULL(json_object_get(&val, "a"));
    return 0;
}

static int test_array_get_valid_index(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("[10,20,30]", 10, &arena, &val));
    const json_value_t *v = json_array_get(&val, 1);
    ASSERT_NOT_NULL(v);
    ASSERT_FLOAT_NEAR(v->number, 20.0, 0.001);
    return 0;
}

static int test_array_get_out_of_range(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("[10]", 4, &arena, &val));
    ASSERT_NULL(json_array_get(&val, 5));
    return 0;
}

static int test_string_copy_truncation(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    ASSERT_TRUE(json_parse("\"abcdef\"", 8, &arena, &val));
    char buf[4];
    /* Buffer too small — should return false. */
    ASSERT_FALSE(json_string_copy(&val, buf, sizeof(buf)));
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Serialization tests                                                      */
/* ----------------------------------------------------------------------- */

static int test_write_null(void) {
    json_value_t val = {.type = JSON_NULL};
    char buf[32];
    size_t n = json_write(&val, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "null");
    ASSERT_INT_EQ((int)n, 4);
    return 0;
}

static int test_write_bool_true(void) {
    json_value_t val = {.type = JSON_BOOL, .boolean = true};
    char buf[32];
    json_write(&val, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "true");
    return 0;
}

static int test_write_number(void) {
    json_value_t val = {.type = JSON_NUMBER, .number = 42.5};
    char buf[32];
    json_write(&val, buf, sizeof(buf));
    /* Should produce "42.5" */
    ASSERT_TRUE(strstr(buf, "42.5") != NULL);
    return 0;
}

static int test_write_integer(void) {
    json_value_t val = {.type = JSON_NUMBER, .number = 7.0};
    char buf[32];
    json_write(&val, buf, sizeof(buf));
    ASSERT_STR_EQ(buf, "7");
    return 0;
}

static int test_roundtrip_object(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input = "{\"id\":1,\"cmd\":\"spawn\",\"ok\":true}";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));

    char buf[256];
    json_write(&val, buf, sizeof(buf));

    /* Re-parse the serialized output and verify values match. */
    json_arena_t arena2 = make_arena();
    json_value_t val2;
    ASSERT_TRUE(json_parse(buf, strlen(buf), &arena2, &val2));
    ASSERT_INT_EQ(val2.type, JSON_OBJECT);

    const json_value_t *id = json_object_get(&val2, "id");
    ASSERT_NOT_NULL(id);
    ASSERT_FLOAT_NEAR(id->number, 1.0, 0.001);

    const json_value_t *cmd = json_object_get(&val2, "cmd");
    ASSERT_NOT_NULL(cmd);
    char cmd_str[32];
    json_string_copy(cmd, cmd_str, sizeof(cmd_str));
    ASSERT_STR_EQ(cmd_str, "spawn");
    return 0;
}

static int test_write_buffer_too_small(void) {
    json_value_t val = {.type = JSON_NULL};
    char buf[3]; /* "null" needs 5 bytes with null terminator. */
    size_t needed = json_write(&val, buf, sizeof(buf));
    /* Should report it needs 4 bytes, but only wrote 2 + null. */
    ASSERT_TRUE(needed >= 4);
    return 0;
}

/* ----------------------------------------------------------------------- */
/* Edit protocol format test (the actual message format)                    */
/* ----------------------------------------------------------------------- */

static int test_parse_edit_protocol_request(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input =
        "{\"id\":42,\"cmd\":\"spawn\","
        "\"args\":{\"type\":\"box\",\"size\":[2,2,2],\"pos\":[10,0,5]}}";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));

    const json_value_t *id = json_object_get(&val, "id");
    ASSERT_NOT_NULL(id);
    ASSERT_FLOAT_NEAR(id->number, 42.0, 0.001);

    const json_value_t *cmd = json_object_get(&val, "cmd");
    ASSERT_NOT_NULL(cmd);
    char cmd_str[32];
    json_string_copy(cmd, cmd_str, sizeof(cmd_str));
    ASSERT_STR_EQ(cmd_str, "spawn");

    const json_value_t *args = json_object_get(&val, "args");
    ASSERT_NOT_NULL(args);
    const json_value_t *pos = json_object_get(args, "pos");
    ASSERT_NOT_NULL(pos);
    ASSERT_UINT_EQ(pos->array.count, 3);
    return 0;
}

static int test_parse_edit_protocol_response(void) {
    json_arena_t arena = make_arena();
    json_value_t val;
    const char *input =
        "{\"id\":42,\"ok\":true,\"result\":{\"entity\":\"box_042\"}}";
    ASSERT_TRUE(json_parse(input, strlen(input), &arena, &val));

    const json_value_t *ok = json_object_get(&val, "ok");
    ASSERT_NOT_NULL(ok);
    ASSERT_TRUE(ok->boolean);

    const json_value_t *result = json_object_get(&val, "result");
    ASSERT_NOT_NULL(result);
    const json_value_t *entity = json_object_get(result, "entity");
    ASSERT_NOT_NULL(entity);
    char ent_str[32];
    json_string_copy(entity, ent_str, sizeof(ent_str));
    ASSERT_STR_EQ(ent_str, "box_042");
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
    /* Happy path */
    {"parse_null",               test_parse_null},
    {"parse_true",               test_parse_true},
    {"parse_false",              test_parse_false},
    {"parse_integer",            test_parse_integer},
    {"parse_negative_number",    test_parse_negative_number},
    {"parse_exponent",           test_parse_exponent},
    {"parse_string",             test_parse_string},
    {"parse_string_escapes",     test_parse_string_escapes},
    {"parse_empty_array",        test_parse_empty_array},
    {"parse_number_array",       test_parse_number_array},
    {"parse_empty_object",       test_parse_empty_object},
    {"parse_simple_object",      test_parse_simple_object},
    /* Nested */
    {"parse_nested_object",      test_parse_nested_object},
    {"parse_array_of_objects",   test_parse_array_of_objects},
    /* Edge cases */
    {"parse_whitespace",         test_parse_whitespace},
    {"parse_empty_string",       test_parse_empty_string},
    {"parse_zero",               test_parse_zero},
    {"parse_negative_exponent",  test_parse_negative_exponent},
    /* Failure modes */
    {"parse_empty_input",        test_parse_empty_input},
    {"parse_truncated_object",   test_parse_truncated_object},
    {"parse_truncated_array",    test_parse_truncated_array},
    {"parse_invalid_token",      test_parse_invalid_token},
    {"parse_trailing_comma_obj", test_parse_trailing_comma_object},
    {"parse_trailing_comma_arr", test_parse_trailing_comma_array},
    {"parse_missing_colon",      test_parse_missing_colon},
    {"parse_dup_key_not_rej",    test_parse_duplicate_key_not_rejected},
    {"parse_arena_exhaustion",   test_parse_arena_exhaustion},
    /* Accessors */
    {"object_get_missing_key",   test_object_get_missing_key},
    {"object_get_on_non_object", test_object_get_on_non_object},
    {"array_get_valid_index",    test_array_get_valid_index},
    {"array_get_out_of_range",   test_array_get_out_of_range},
    {"string_copy_truncation",   test_string_copy_truncation},
    /* Serialization */
    {"write_null",               test_write_null},
    {"write_bool_true",          test_write_bool_true},
    {"write_number",             test_write_number},
    {"write_integer",            test_write_integer},
    {"roundtrip_object",         test_roundtrip_object},
    {"write_buffer_too_small",   test_write_buffer_too_small},
    /* Protocol format */
    {"edit_protocol_request",    test_parse_edit_protocol_request},
    {"edit_protocol_response",   test_parse_edit_protocol_response},
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
