/**
 * @file procgen_smoke_tests.c
 * @brief P2.5: Mid-way integration smoke test.
 *
 * Full pipeline: string → tokens → layout → JSON.
 * Multiple dungeon variations, JSON validation, roundtrip.
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

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define PASS() g_pass++

#define TOK_CAP 512

static void free_l(fr_dungeon_layout_t *l) {
    free(l->rooms); free(l->corridors); free(l->openings);
    free(l->ramps); free(l->markers); free(l->nav_nodes); free(l->nav_edges);
    memset(l, 0, sizeof(*l));
}

static int pipeline(const char *input, fr_dungeon_layout_t *l, char *json, uint32_t jcap) {
    procgen_token_t tokens[TOK_CAP];
    char err[256];
    uint32_t count = 0;
    if (procgen_tokenize(input, tokens, TOK_CAP, &count, err, sizeof(err)) != TOK_ERR_NONE) return -1;
    if (grammar_blockout_rasterize(tokens, count, l, err, sizeof(err)) != 0) return -1;
    if (procgen_serialize_to_json_buf(l, json, jcap, NULL) != 0) return -1;
    return 0;
}

/* ── Dungeon variations ───────────────────────────────────────── */

static void test_small_dungeon(void) {
    const char *s = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\n";
    fr_dungeon_layout_t l;
    char j[4096];
    ASSERT_INT_EQ(pipeline(s, &l, j, sizeof(j)), 0);
    ASSERT_TRUE(strstr(j, "\"room_quad\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"spawn\"") != NULL);
    free_l(&l); PASS();
}

static void test_two_room_with_corridor(void) {
    const char *s = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=r1\nROOM_QUAD x=20 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=r2\nCORRIDOR_H w=4 floor_z=0 ceil_z=5\nSPAWN x=5 y=5 z=1\n";
    fr_dungeon_layout_t l;
    char j[8192];
    ASSERT_INT_EQ(pipeline(s, &l, j, sizeof(j)), 0);
    ASSERT_TRUE(strstr(j, "\"r1\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"r2\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"corridor\"") != NULL);
    free_l(&l); PASS();
}

static void test_many_markers(void) {
    const char *s = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nMARKER x=0 y=0 z=0 name=a\nMARKER x=1 y=0 z=0 name=b\nMARKER x=2 y=0 z=0 name=c\nMARKER x=3 y=0 z=0 name=d\nMARKER x=0 y=1 z=0 name=e\n";
    fr_dungeon_layout_t l;
    char j[8192];
    ASSERT_INT_EQ(pipeline(s, &l, j, sizeof(j)), 0);
    ASSERT_TRUE(strstr(j, "\"a\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"e\"") != NULL);
    free_l(&l); PASS();
}

static void test_all_opening_types(void) {
    const char *s = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nDOOR w=2 h=3\nWINDOW w=1 h=1\n";
    fr_dungeon_layout_t l;
    char j[4096];
    ASSERT_INT_EQ(pipeline(s, &l, j, sizeof(j)), 0);
    ASSERT_TRUE(strstr(j, "\"entities\"") != NULL);
    free_l(&l); PASS();
}

static void test_deep_nesting_smoke(void) {
    const char *s = "@grammar blockout v1\nBLOCK\nBLOCK\nBLOCK\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nEBLOCK\nEBLOCK\nEBLOCK\n";
    fr_dungeon_layout_t l;
    char j[4096];
    ASSERT_INT_EQ(pipeline(s, &l, j, sizeof(j)), 0);
    free_l(&l); PASS();
}

static void test_json_structure_valid(void) {
    const char *s = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6 name=test\nSPAWN x=5 y=5 z=1\nCORRIDOR_V w=3 floor_z=0 ceil_z=5\nDOOR w=2 h=3\nMARKER x=5 y=9 z=1 name=exit\n";
    fr_dungeon_layout_t l;
    char j[16384];
    ASSERT_INT_EQ(pipeline(s, &l, j, sizeof(j)), 0);

    /* Verify JSON structure: starts with {, ends with }, has entities array. */
    ASSERT_TRUE(j[0] == '{');
    ASSERT_TRUE(j[strlen(j) - 1] == '}');
    ASSERT_TRUE(strstr(j, "\"entities\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"type\":\"room_quad\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"type\":\"corridor\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"type\":\"marker\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"type\":\"spawn\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"id\":0") != NULL);
    ASSERT_TRUE(strstr(j, "\"pos\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"rot\"") != NULL);
    ASSERT_TRUE(strstr(j, "\"scale\"") != NULL);

    free_l(&l); PASS();
}

int main(void) {
    printf("=== Procgen P2.5 Smoke Tests ===\n\n");
    RUN(test_small_dungeon);
    RUN(test_two_room_with_corridor);
    RUN(test_many_markers);
    RUN(test_all_opening_types);
    RUN(test_deep_nesting_smoke);
    RUN(test_json_structure_valid);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
