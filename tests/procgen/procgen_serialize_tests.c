/**
 * @file procgen_serialize_tests.c
 * @brief P2 tests: serialize fr_dungeon_layout_t → JSON and verify.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/grammar_blockout.h"
#include "ferrum/procgen/procgen_serialize.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn)                                                              \
    do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr)                                                    \
    do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b)          ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b)      ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_STREQ(a, b)       ASSERT_TRUE(strcmp((a), (b)) == 0)
#define PASS()                   g_pass++

#define TOK_BUF_CAP 256

static void free_layout(fr_dungeon_layout_t *l) {
    free(l->rooms); free(l->corridors); free(l->openings);
    free(l->ramps); free(l->markers); free(l->nav_nodes); free(l->nav_edges);
    memset(l, 0, sizeof(*l));
}

/* ── JSON roundtrip via buffer ────────────────────────────────── */

static void test_serialize_single_room_to_json(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6 name=entrance\n"
        "SPAWN x=5 y=5 z=1\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    tok_error_t rc = procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err));
    ASSERT_INT_EQ(rc, (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)), 0);

    char json_buf[4096];
    uint32_t out_len = 0;
    ASSERT_INT_EQ(procgen_serialize_to_json_buf(&layout, json_buf, sizeof(json_buf), &out_len), 0);
    ASSERT_TRUE(out_len > 0);

    /* Verify JSON structure: contains entities array. */
    ASSERT_TRUE(strstr(json_buf, "\"entities\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"room_quad\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"spawn\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"id\":0") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"name\":\"entrance\"") != NULL);

    free_layout(&layout);
    PASS();
}

/* ── Serialize with markers ───────────────────────────────────── */

static void test_serialize_markers(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6\n"
        "SPAWN x=5 y=5 z=1\n"
        "MARKER x=3 y=3 z=1 name=treasure\n"
        "MARKER x=7 y=7 z=1 name=exit\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)), 0);

    char json_buf[4096];
    uint32_t out_len = 0;
    ASSERT_INT_EQ(procgen_serialize_to_json_buf(&layout, json_buf, sizeof(json_buf), &out_len), 0);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"marker\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"name\":\"treasure\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"name\":\"exit\"") != NULL);

    free_layout(&layout);
    PASS();
}

/* ── Multi-room dungeon ───────────────────────────────────────── */

static void test_serialize_multi_room_dungeon(void) {
    const char *input =
        "@grammar blockout v1\n"
        "BLOCK\n"
        "  ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=room1\n"
        "  SPAWN x=5 y=5 z=1\n"
        "  CORRIDOR_H w=4 floor_z=0 ceil_z=5\n"
        "  DOOR w=3 h=4\n"
        "  ROOM_PENT floor_z=0 ceil_z=8 name=arena\n"
        "  RAMP_UP dz=4 w=3\n"
        "  MARKER x=15 y=15 z=1 name=exit\n"
        "EBLOCK\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)), 0);

    char json_buf[16384];
    ASSERT_INT_EQ(procgen_serialize_to_json_buf(&layout, json_buf, sizeof(json_buf), NULL), 0);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"room_quad\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"room_pent\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"corridor\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"marker\"") != NULL);
    ASSERT_TRUE(strstr(json_buf, "\"type\":\"spawn\"") != NULL);

    free_layout(&layout);
    PASS();
}

/* ── File output roundtrip ────────────────────────────────────── */

static void test_serialize_to_file(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=test\n"
        "SPAWN x=5 y=5 z=1\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)), 0);

    const char *path = "/tmp/procgen_test_level.json";
    ASSERT_INT_EQ(procgen_serialize_to_json(&layout, path, err, sizeof(err)), 0);

    /* Verify file exists and has content. */
    FILE *f = fopen(path, "r");
    ASSERT_TRUE(f != NULL);
    char line[256];
    ASSERT_TRUE(fgets(line, sizeof(line), f) != NULL);
    ASSERT_TRUE(strstr(line, "\"entities\"") != NULL);
    fclose(f);
    remove(path);

    free_layout(&layout);
    PASS();
}

/* ── Buffer overflow detection ────────────────────────────────── */

static void test_buffer_overflow(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6\n"
        "SPAWN x=5 y=5 z=1\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)), 0);

    /* Tiny buffer — should overflow. */
    char small[16];
    ASSERT_INT_EQ(procgen_serialize_to_json_buf(&layout, small, sizeof(small), NULL), -1);

    free_layout(&layout);
    PASS();
}

/* ── procgen_serialize_level alias ────────────────────────────── */

static void test_serialize_level_alias(void) {
    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\n"
        "SPAWN x=1 y=1 z=1\n";
    procgen_token_t tokens[TOK_BUF_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_BUF_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(grammar_blockout_rasterize(tokens, count, &layout, err, sizeof(err)), 0);

    const char *path = "/tmp/procgen_level_test.json";
    ASSERT_INT_EQ(procgen_serialize_level(&layout, path, err, sizeof(err)), 0);

    FILE *f = fopen(path, "r");
    ASSERT_TRUE(f != NULL);
    fclose(f);
    remove(path);

    free_layout(&layout);
    PASS();
}

/* ================================================================= */

int main(void) {
    printf("=== Procgen Serialize Tests ===\n\n");

    RUN(test_serialize_single_room_to_json);
    RUN(test_serialize_markers);
    RUN(test_serialize_multi_room_dungeon);
    RUN(test_serialize_to_file);
    RUN(test_buffer_overflow);
    RUN(test_serialize_level_alias);

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
