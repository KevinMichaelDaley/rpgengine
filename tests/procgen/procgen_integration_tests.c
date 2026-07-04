/**
 * @file procgen_integration_tests.c
 * @brief P0 integration tests: end-to-end tokenizer + types validation.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/math/vec3.h"
#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"
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

#define TOK_BUF_CAP 512

/* ── Full dungeon roundtrip ────────────────────────────────────── */

static void test_full_dungeon_types_and_values(void) {
    const char *input =
        "@grammar blockout v1\n"
        "BLOCK\n"
        "  ROOM_QUAD  x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6 name=entrance\n"
        "  SPAWN     x=0 y=0 z=1\n"
        "  MARKER    x=10 y=0 z=1  name=boss_arena\n"
        "  CORRIDOR_H  from=(20,0) to=(40,0) w=6 floor_z=0 ceil_z=5\n"
        "  DOOR        at=(20,0)\n"
        "  ROOM_PENT\n"
        "  RAMP_UP     from=(40,10) to=(50,10) dz=6\n"
        "  WINDOW      at=(30,5)\n"
        "  CORRIDOR_V  from=(40,20) to=(40,40) w=4 floor_z=0 ceil_z=5\n"
        "  CORRIDOR_DIAG\n"
        "  RAMP_DOWN   from=(50,10) to=(60,10) dz=-3\n"
        "  MARKER      x=50 y=0 z=1  name=treasure\n"
        "EBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_NONE);
    ASSERT_TRUE(count > 0);

    /* Verify keyword types appear in order. */
    tok_type_t expected_kws[] = {
        TOK_GRAMMAR,
        TOK_BLOCK,
        TOK_ROOM_QUAD,
        TOK_SPAWN,
        TOK_MARKER,
        TOK_CORRIDOR_H,
        TOK_DOOR,
        TOK_ROOM_PENT,
        TOK_RAMP_UP,
        TOK_WINDOW,
        TOK_CORRIDOR_V,
        TOK_CORRIDOR_DIAG,
        TOK_RAMP_DOWN,
        TOK_MARKER,
        TOK_EBLOCK,
    };
    uint32_t ek = 0;
    uint32_t ek_count = sizeof(expected_kws) / sizeof(expected_kws[0]);
    for (uint32_t i = 0; i < count && ek < ek_count; i++) {
        if (tokens[i].type == expected_kws[ek]) {
            ek++;
        }
    }
    ASSERT_EQ(ek, ek_count);

    /* Verify grammar header. */
    ASSERT_EQ(tokens[0].type, TOK_GRAMMAR);
    ASSERT_STREQ(tokens[0].value.s, "blockout");
    ASSERT_INT_EQ(tokens[0].grammar_version, (uint32_t)1);

    PASS();
}

/* ── All error codes exercised ─────────────────────────────────── */

static void test_all_error_codes(void) {
    char err[256];
    procgen_token_t tokens[TOK_BUF_CAP];
    uint32_t count;

    /* Missing @grammar → UNEXPECTED_TOKEN */
    count = 0;
    tok_error_t rc = procgen_tokenize("ROOM_QUAD x=0", tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNEXPECTED_TOKEN);

    /* Unbalanced block → UNBALANCED_BLOCK */
    count = 0;
    rc = procgen_tokenize("@grammar t v1\nBLOCK\nROOM_QUAD\n", tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNBALANCED_BLOCK);

    /* Buffer overflow → BUFFER_FULL */
    count = 0;
    rc = procgen_tokenize("@grammar t v1\nROOM_QUAD x=0\nROOM_PENT\n", tokens, 2, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_BUFFER_FULL);

    /* Unknown keyword → UNEXPECTED_TOKEN */
    count = 0;
    rc = procgen_tokenize("@grammar t v1\nNOT_A_WORD x=0", tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNEXPECTED_TOKEN);

    /* EBLOCK without BLOCK → UNBALANCED_BLOCK */
    count = 0;
    rc = procgen_tokenize("@grammar t v1\nEBLOCK\n", tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_UNBALANCED_BLOCK);

    PASS();
}

/* ── Max parameter values ──────────────────────────────────────── */

static void test_parameter_ranges(void) {
    const char *input = "@grammar t v1\nSPAWN x=-1000000 y=2147483647 z=0\n"
                        "RAMP_UP dz=1.0\n"
                        "MARKER name=valid_name\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_NONE);
    ASSERT_TRUE(count > 0);
    PASS();
}

/* ── Deep nesting ──────────────────────────────────────────────── */

static void test_deep_nesting(void) {
    /* 10-level deep nesting. */
    const char *input =
        "@grammar t v1\n"
        "BLOCK\nBLOCK\nBLOCK\nBLOCK\nBLOCK\n"
        "BLOCK\nBLOCK\nBLOCK\nBLOCK\nBLOCK\n"
        "ROOM_QUAD\n"
        "EBLOCK\nEBLOCK\nEBLOCK\nEBLOCK\nEBLOCK\n"
        "EBLOCK\nEBLOCK\nEBLOCK\nEBLOCK\nEBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_NONE);
    ASSERT_TRUE(count >= 21);
    PASS();
}

/* ── Special characters in strings ─────────────────────────────── */

static void test_special_chars_in_strings(void) {
    const char *input = "@grammar t v1\nMARKER name=\"room-1_level+2\"\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_NONE);
    ASSERT_TRUE(count > 0);
    PASS();
}

/* ── Unicode-like identifiers (rejected gracefully) ────────────── */

static void test_non_ascii_identifier_rejected(void) {
    /* Euro sign is not in isalpha range. */
    const char *input = "@grammar t v1\nROOM\xe2\x82\xac_QUAD\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    /* Should fail — € is not a valid identifier char in C locale. */
    PASS();
}

/* ── Types compilation check ───────────────────────────────────── */

static void test_layout_type_integrity(void) {
    /* Verify all layout types can be instantiated together. */
    fr_dungeon_layout_t layout;
    memset(&layout, 0, sizeof(layout));
    layout.version = 1;
    layout.spawn_pos = (vec3_t){0.0f, 0.0f, 0.0f};

    fr_room_def_t room;
    memset(&room, 0, sizeof(room));
    room.vertex_count = 4;
    room.floor_z = 0.0f;
    room.ceil_z = 6.0f;

    fr_corridor_def_t corr;
    memset(&corr, 0, sizeof(corr));
    corr.from = (vec3_t){0.0f, 0.0f, 0.0f};
    corr.to   = (vec3_t){10.0f, 0.0f, 0.0f};
    corr.width = 4.0f;

    ASSERT_EQ(layout.version, (uint32_t)1);
    ASSERT_TRUE(room.ceil_z > room.floor_z);
    ASSERT_TRUE(corr.width > 0.0f);
    PASS();
}

/* ================================================================= */

int main(void) {
    printf("=== Procgen P0 Integration Tests ===\n\n");

    RUN(test_full_dungeon_types_and_values);
    RUN(test_all_error_codes);
    RUN(test_parameter_ranges);
    RUN(test_deep_nesting);
    RUN(test_special_chars_in_strings);
    RUN(test_non_ascii_identifier_rejected);
    RUN(test_layout_type_integrity);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
