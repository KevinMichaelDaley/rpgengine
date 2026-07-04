/**
 * @file procgen_tokenize_tests.c
 * @brief RED: Tests for the procgen tokenizer.
 *
 * Validates: keyword recognition, parameter parsing,
 * BLOCK/EBLOCK nesting, comment skipping, error handling,
 * line/col tracking, and @grammar header parsing.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_tokenize.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                                              \
    do {                                                                     \
        printf("RUN  %s\n", #fn);                                           \
        fn();                                                                \
        printf("OK   %s\n", #fn);                                           \
    } while (0)

#define ASSERT_TRUE(expr)                                                    \
    do {                                                                     \
        if (!(expr)) {                                                       \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);           \
            g_fail++;                                                        \
            return;                                                          \
        }                                                                    \
    } while (0)

#define ASSERT_EQ(a, b)          ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b)      ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_STREQ(a, b)       ASSERT_TRUE(strcmp((a), (b)) == 0)
#define PASS()                   g_pass++

/* Token buffer for test helpers */
#define TOK_BUF_CAP 256

/* ── @grammar header ───────────────────────────────────────────── */

static void test_parse_grammar_header(void) {
    const char *input = "@grammar blockout v1\nROOM_QUAD x=0";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 2);
    ASSERT_EQ(tokens[0].type, TOK_GRAMMAR);
    ASSERT_STREQ(tokens[0].value.s, "blockout");
    ASSERT_INT_EQ(tokens[0].grammar_version, (uint32_t)1);
    PASS();
}

static void test_missing_grammar_header_fails(void) {
    const char *input = "ROOM_QUAD x=0";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNEXPECTED_TOKEN);
    ASSERT_TRUE(err[0] != '\0');
    PASS();
}

/* ── Basic keyword recognition ─────────────────────────────────── */

static void test_recognize_room_quad(void) {
    const char *input = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6 name=entrance";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 2);
    ASSERT_EQ(tokens[1].type, TOK_ROOM_QUAD);
    PASS();
}

static void test_recognize_all_keywords(void) {
    const char *input =
        "@grammar test v1\n"
        "ROOM_QUAD\n"
        "ROOM_PENT\n"
        "CORRIDOR_H\n"
        "CORRIDOR_V\n"
        "CORRIDOR_DIAG\n"
        "RAMP_UP\n"
        "RAMP_DOWN\n"
        "DOOR\n"
        "WINDOW\n"
        "SPAWN\n"
        "MARKER\n"
        "BLOCK\n"
        "EBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);

    tok_type_t expected[] = {
        TOK_GRAMMAR, TOK_ROOM_QUAD, TOK_ROOM_PENT,
        TOK_CORRIDOR_H, TOK_CORRIDOR_V, TOK_CORRIDOR_DIAG,
        TOK_RAMP_UP, TOK_RAMP_DOWN, TOK_DOOR, TOK_WINDOW,
        TOK_SPAWN, TOK_MARKER, TOK_BLOCK, TOK_EBLOCK
    };
    uint32_t exp_count = sizeof(expected) / sizeof(expected[0]);
    ASSERT_TRUE(count >= exp_count);
    for (uint32_t i = 0; i < exp_count; i++) {
        ASSERT_EQ(tokens[i].type, expected[i]);
    }
    PASS();
}

static void test_unknown_keyword_fails(void) {
    const char *input = "@grammar test v1\nINVALID_KW x=0";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNEXPECTED_TOKEN);
    ASSERT_TRUE(err[0] != '\0');
    PASS();
}

/* ── Parameter parsing ─────────────────────────────────────────── */

static void test_parse_integer_param(void) {
    const char *input = "@grammar test v1\nSPAWN x=42 y=-5 z=0";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 4);  /* GRAMMAR, SPAWN, x param, y param, z param */
    PASS();
}

static void test_parse_float_param(void) {
    const char *input = "@grammar test v1\nRAMP_UP dz=6.5";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 2);
    PASS();
}

static void test_parse_string_param_quoted(void) {
    const char *input = "@grammar test v1\nMARKER name=\"boss arena\"";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 2);
    PASS();
}

static void test_parse_string_param_unquoted(void) {
    const char *input = "@grammar test v1\nROOM_QUAD name=entrance";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 2);
    PASS();
}

static void test_parse_coord_param(void) {
    const char *input = "@grammar test v1\nDOOR at=(10,5)";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 2);
    PASS();
}

/* ── BLOCK/EBLOCK nesting ──────────────────────────────────────── */

static void test_block_eblock_nesting_valid(void) {
    const char *input =
        "@grammar test v1\n"
        "BLOCK\n"
        "  ROOM_QUAD x=0\n"
        "  BLOCK\n"
        "    ROOM_PENT\n"
        "  EBLOCK\n"
        "EBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    /* Verify nested blocks: GRAMMAR, BLOCK, ROOM_QUAD, BLOCK, ROOM_PENT, EBLOCK, EBLOCK */
    ASSERT_TRUE(count >= 8);
    ASSERT_EQ(tokens[1].type, TOK_BLOCK);
    ASSERT_EQ(tokens[4].type, TOK_BLOCK);
    ASSERT_EQ(tokens[6].type, TOK_EBLOCK);
    ASSERT_EQ(tokens[7].type, TOK_EBLOCK);
    PASS();
}

static void test_unbalanced_block_fails(void) {
    const char *input =
        "@grammar test v1\n"
        "BLOCK\n"
        "  ROOM_QUAD x=0\n"
        "EBLOCK\n"
        "EBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNBALANCED_BLOCK);
    ASSERT_TRUE(err[0] != '\0');
    PASS();
}

static void test_missing_eblock_fails(void) {
    const char *input =
        "@grammar test v1\n"
        "BLOCK\n"
        "  ROOM_QUAD x=0\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNBALANCED_BLOCK);
    ASSERT_TRUE(err[0] != '\0');
    PASS();
}

static void test_eblock_without_block_fails(void) {
    const char *input = "@grammar test v1\nEBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNBALANCED_BLOCK);
    ASSERT_TRUE(err[0] != '\0');
    PASS();
}

/* ── Comment handling ──────────────────────────────────────────── */

static void test_comments_ignored(void) {
    const char *input =
        "@grammar test v1\n"
        "# This is a comment\n"
        "ROOM_QUAD x=0  # inline comment\n"
        "ROOM_PENT\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    ASSERT_TRUE(count >= 4);  /* GRAMMAR, ROOM_QUAD, x=0 param, ROOM_PENT */
    ASSERT_EQ(tokens[1].type, TOK_ROOM_QUAD);
    ASSERT_EQ(tokens[3].type, TOK_ROOM_PENT);
    PASS();
}

/* ── Line/col tracking ─────────────────────────────────────────── */

static void test_line_col_tracking(void) {
    const char *input =
        "@grammar test v1\n"
        "\n"
        "ROOM_QUAD x=0\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    /* @grammar is line 1, ROOM_QUAD is line 3 */
    ASSERT_EQ(tokens[0].line, (uint32_t)1);
    ASSERT_EQ(tokens[1].line, (uint32_t)3);
    PASS();
}

static void test_error_reports_line_col(void) {
    const char *input = "@grammar test v1\nINVALID_KW";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, -1);
    /* Error message should contain line number. */
    ASSERT_TRUE(strstr(err, "2") != NULL || strstr(err, "line") != NULL);
    PASS();
}

/* ── Buffer overflow ───────────────────────────────────────────── */

static void test_buffer_overflow(void) {
    /* Only 3 token slots — must fail on a 4-token string. */
    const char *input = "@grammar test v1\nROOM_QUAD x=0\nROOM_PENT\n";
    procgen_token_t tokens[3];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, 3, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_BUFFER_FULL);
    PASS();
}

/* ── Empty input ───────────────────────────────────────────────── */

static void test_empty_input_fails(void) {
    const char *input = "";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNEXPECTED_TOKEN);
    PASS();
}

static void test_whitespace_only_fails(void) {
    const char *input = "   \n  \n   ";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNEXPECTED_TOKEN);
    PASS();
}

/* ── Full dungeon string ───────────────────────────────────────── */

static void test_full_dungeon_string(void) {
    const char *input =
        "@grammar blockout v1\n"
        "BLOCK\n"
        "  ROOM_QUAD  x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6  name=entrance\n"
        "  SPAWN     x=0 y=0 z=1\n"
        "  MARKER    x=10 y=0 z=1  name=first_encounter\n"
        "  CORRIDOR_H  from=(20,0) to=(40,0) w=6 floor_z=0 ceil_z=5\n"
        "  DOOR        at=(20,0)\n"
        "  ROOM_PENT   # 5-sided room\n"
        "  RAMP_UP     from=(40,10) to=(50,10) dz=6\n"
        "EBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    int rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, 0);
    /* Should have all token types in order. */
    ASSERT_TRUE(count >= 10);
    ASSERT_EQ(tokens[0].type, TOK_GRAMMAR);
    ASSERT_EQ(tokens[1].type, TOK_BLOCK);
    ASSERT_EQ(tokens[2].type, TOK_ROOM_QUAD);
    ASSERT_EQ(tokens[count - 1].type, TOK_EBLOCK);
    PASS();
}

/* ================================================================= */

int main(void) {
    printf("=== Procgen Tokenize Tests ===\n\n");

    RUN(test_parse_grammar_header);
    RUN(test_missing_grammar_header_fails);
    RUN(test_recognize_room_quad);
    RUN(test_recognize_all_keywords);
    RUN(test_unknown_keyword_fails);
    RUN(test_parse_integer_param);
    RUN(test_parse_float_param);
    RUN(test_parse_string_param_quoted);
    RUN(test_parse_string_param_unquoted);
    RUN(test_parse_coord_param);
    RUN(test_block_eblock_nesting_valid);
    RUN(test_unbalanced_block_fails);
    RUN(test_missing_eblock_fails);
    RUN(test_eblock_without_block_fails);
    RUN(test_comments_ignored);
    RUN(test_line_col_tracking);
    RUN(test_error_reports_line_col);
    RUN(test_buffer_overflow);
    RUN(test_empty_input_fails);
    RUN(test_whitespace_only_fails);
    RUN(test_full_dungeon_string);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
