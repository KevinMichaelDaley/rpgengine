/**
 * @file procgen_grammar_registry_tests.c
 * @brief P3 tests: grammar registry, blockout registration, runtime selection.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/procgen/procgen_types.h"
#include "ferrum/procgen/procgen_layout.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/grammar_blockout.h"
#include "ferrum/procgen/procgen_grammar_registry.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_STREQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)
#define PASS() g_pass++

#define TOK_CAP 256

static void free_l(fr_dungeon_layout_t *l) {
    free(l->rooms); free(l->corridors); free(l->openings);
    free(l->ramps); free(l->markers);
    memset(l, 0, sizeof(*l));
}

/* ── Registry basics ───────────────────────────────────────────── */

static void test_registry_init_empty(void) {
    procgen_grammar_registry_clear();
    ASSERT_INT_EQ(procgen_grammar_count(), (uint32_t)0);
    ASSERT_TRUE(procgen_grammar_find("nonexistent") == NULL);
    PASS();
}

static void test_register_and_find(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g;
    memset(&g, 0, sizeof(g));
    g.name     = "test_grammar";
    g.version  = 1;
    g.tokenize = procgen_tokenize;
    g.rasterize = grammar_blockout_rasterize;

    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);
    ASSERT_INT_EQ(procgen_grammar_count(), (uint32_t)1);

    const procgen_grammar_t *found = procgen_grammar_find("test_grammar");
    ASSERT_TRUE(found != NULL);
    ASSERT_STREQ(found->name, "test_grammar");
    ASSERT_INT_EQ(found->version, (uint32_t)1);
    PASS();
}

static void test_duplicate_rejected(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g;
    memset(&g, 0, sizeof(g));
    g.name     = "dup";
    g.version  = 1;
    g.tokenize = procgen_tokenize;
    g.rasterize = grammar_blockout_rasterize;

    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);
    ASSERT_INT_EQ(procgen_grammar_register(&g), -1);
    PASS();
}

static void test_multiple_grammars(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g1 = {"grammar_a", 1, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};
    procgen_grammar_t g2 = {"grammar_b", 2, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};

    ASSERT_INT_EQ(procgen_grammar_register(&g1), 0);
    ASSERT_INT_EQ(procgen_grammar_register(&g2), 0);
    ASSERT_INT_EQ(procgen_grammar_count(), (uint32_t)2);
    ASSERT_TRUE(procgen_grammar_find("grammar_a") != NULL);
    ASSERT_TRUE(procgen_grammar_find("grammar_b") != NULL);
    PASS();
}

/* ── Runtime grammar selection via @grammar header ─────────────── */

static void test_rasterize_with_registry(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g;
    memset(&g, 0, sizeof(g));
    g.name      = "blockout";
    g.version   = 1;
    g.tokenize  = procgen_tokenize;
    g.rasterize = grammar_blockout_rasterize;
    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);

    const char *input =
        "@grammar blockout v1\n"
        "ROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\n"
        "SPAWN x=1 y=1 z=1\n";
    procgen_token_t tokens[TOK_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(procgen_rasterize_with_registry(tokens, count, &layout, err, sizeof(err)), 0);
    ASSERT_INT_EQ(layout.room_count, (uint32_t)1);
    free_l(&layout);
    PASS();
}

static void test_unknown_grammar_fails(void) {
    procgen_grammar_registry_clear();

    const char *input = "@grammar nonexistent v1\nROOM_QUAD x=0\nSPAWN x=1 y=1 z=1\n";
    procgen_token_t tokens[TOK_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    ASSERT_INT_EQ(procgen_rasterize_with_registry(tokens, count, &layout, err, sizeof(err)), -1);
    ASSERT_TRUE(strstr(err, "unknown grammar") != NULL);
    PASS();
}

static void test_grammar_switching(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g1 = {"blockout", 1, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};
    procgen_grammar_t g2 = {"mock_v2", 2, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};
    ASSERT_INT_EQ(procgen_grammar_register(&g1), 0);
    ASSERT_INT_EQ(procgen_grammar_register(&g2), 0);

    /* First string uses blockout. */
    const char *s1 = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\n";
    procgen_token_t t1[TOK_CAP];
    char err[256];
    uint32_t c1 = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(s1, t1, TOK_CAP, &c1, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t l1;
    ASSERT_INT_EQ(procgen_rasterize_with_registry(t1, c1, &l1, err, sizeof(err)), 0);
    ASSERT_STREQ(l1.grammar_name, "blockout");
    free_l(&l1);

    /* Second string uses mock_v2. */
    const char *s2 = "@grammar mock_v2 v2\nROOM_QUAD x=10 y=10 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=11 y=11 z=1\n";
    procgen_token_t t2[TOK_CAP];
    uint32_t c2 = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(s2, t2, TOK_CAP, &c2, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t l2;
    ASSERT_INT_EQ(procgen_rasterize_with_registry(t2, c2, &l2, err, sizeof(err)), 0);
    ASSERT_STREQ(l2.grammar_name, "mock_v2");
    free_l(&l2);
    PASS();
}

/* ── Version mismatch warning ──────────────────────────────────── */

static void test_version_mismatch_non_fatal(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g;
    memset(&g, 0, sizeof(g));
    g.name      = "blockout";
    g.version   = 1;
    g.tokenize  = procgen_tokenize;
    g.rasterize = grammar_blockout_rasterize;
    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);

    /* Request v2 but registered is v1 — should still work. */
    const char *input = "@grammar blockout v2\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\n";
    procgen_token_t tokens[TOK_CAP];
    char err[256];
    uint32_t count = 0;
    ASSERT_INT_EQ((int)procgen_tokenize(input, tokens, TOK_CAP, &count, err, sizeof(err)), (int)TOK_ERR_NONE);

    fr_dungeon_layout_t layout;
    /* Should succeed but warn. */
    ASSERT_INT_EQ(procgen_rasterize_with_registry(tokens, count, &layout, err, sizeof(err)), 0);
    ASSERT_INT_EQ(layout.room_count, (uint32_t)1);
    free_l(&layout);
    PASS();
}

int main(void) {
    printf("=== Procgen Grammar Registry Tests ===\n\n");
    RUN(test_registry_init_empty);
    RUN(test_register_and_find);
    RUN(test_duplicate_rejected);
    RUN(test_multiple_grammars);
    RUN(test_rasterize_with_registry);
    RUN(test_unknown_grammar_fails);
    RUN(test_grammar_switching);
    RUN(test_version_mismatch_non_fatal);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
