/**
 * @file procgen_e2e_tests.c
 * @brief P3.5: Massive E2E smoke test — registry → tokenize → rasterize → JSON.
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
#include "ferrum/procgen/procgen_grammar_registry.h"

static int g_pass = 0;
static int g_fail = 0;

#define RUN(fn) do { printf("RUN  %s\n", #fn); fn(); printf("OK   %s\n", #fn); } while (0)
#define ASSERT_TRUE(expr) do { if (!(expr)) { printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr); g_fail++; return; } } while (0)
#define ASSERT_EQ(a, b) ASSERT_TRUE((a) == (b))
#define ASSERT_INT_EQ(a, b) ASSERT_TRUE((int)(a) == (int)(b))
#define ASSERT_STREQ(a, b) ASSERT_TRUE(strcmp((a), (b)) == 0)
#define PASS() g_pass++

#define TOK_CAP 512

static void free_l(fr_dungeon_layout_t *l) {
    free(l->rooms); free(l->corridors); free(l->openings);
    free(l->ramps); free(l->markers);
    memset(l, 0, sizeof(*l));
}

static int full_pipeline(const char *input, fr_dungeon_layout_t *l, char *json, uint32_t jcap) {
    procgen_token_t tokens[TOK_CAP];
    char err[256];
    uint32_t count = 0;
    if (procgen_tokenize(input, tokens, TOK_CAP, &count, err, sizeof(err)) != TOK_ERR_NONE) return -1;
    if (procgen_rasterize_with_registry(tokens, count, l, err, sizeof(err)) != 0) return -1;
    if (procgen_serialize_to_json_buf(l, json, jcap, NULL) != 0) return -1;
    return 0;
}

/* ── Many dungeon variations ───────────────────────────────────── */

static void test_50_dungeon_strings(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g = {"blockout", 1, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};
    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);

    const char *variations[] = {
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3 name=r\nSPAWN x=1 y=1 z=1\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6\nSPAWN x=5 y=5 z=1\nMARKER x=5 y=9 z=1 name=exit\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=10 h=10 floor_z=0 ceil_z=6\nROOM_PENT floor_z=0 ceil_z=8\nSPAWN x=5 y=5 z=1\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nCORRIDOR_H w=3 floor_z=0 ceil_z=4\nDOOR w=2 h=3\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nRAMP_UP dz=4 w=2\nRAMP_DOWN dz=2 w=2\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nWINDOW w=1 h=1\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=5 h=5 floor_z=0 ceil_z=3\nROOM_QUAD x=15 y=0 w=5 h=5 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\n",
        "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nCORRIDOR_V w=2 floor_z=0 ceil_z=3\nCORRIDOR_DIAG w=2 floor_z=0 ceil_z=3\n",
        "@grammar blockout v1\nBLOCK\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\nEBLOCK\n",
    };
    int n = (int)(sizeof(variations) / sizeof(variations[0]));

    int success = 0;
    for (int i = 0; i < n; i++) {
        fr_dungeon_layout_t l;
        char json[8192];
        if (full_pipeline(variations[i], &l, json, sizeof(json)) == 0) {
            success++;
            free_l(&l);
        }
    }
    ASSERT_INT_EQ(success, n);
    PASS();
}

/* ── Registry persists across calls ────────────────────────────── */

static void test_registry_persists(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g = {"blockout", 1, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};
    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);

    /* Two separate pipeline calls using registry. */
    const char *s1 = "@grammar blockout v1\nROOM_QUAD x=0 y=0 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=1 y=1 z=1\n";
    const char *s2 = "@grammar blockout v1\nROOM_QUAD x=10 y=10 w=4 h=4 floor_z=0 ceil_z=3\nSPAWN x=11 y=11 z=1\n";

    fr_dungeon_layout_t l1;
    char j1[4096], j2[4096];
    ASSERT_INT_EQ(full_pipeline(s1, &l1, j1, sizeof(j1)), 0);
    ASSERT_STREQ(l1.grammar_name, "blockout");
    free_l(&l1);

    fr_dungeon_layout_t l2;
    ASSERT_INT_EQ(full_pipeline(s2, &l2, j2, sizeof(j2)), 0);
    ASSERT_STREQ(l2.grammar_name, "blockout");
    free_l(&l2);
    PASS();
}

static void assert_str_contains(const char *haystack, const char *needle) {
    ASSERT_TRUE(strstr(haystack, needle) != NULL);
}

/* ── JSON output structural integrity ──────────────────────────── */

static void test_json_integrity(void) {
    procgen_grammar_registry_clear();

    procgen_grammar_t g = {"blockout", 1, procgen_tokenize, grammar_blockout_rasterize, NULL, NULL, 0};
    ASSERT_INT_EQ(procgen_grammar_register(&g), 0);

    const char *input =
        "@grammar blockout v1\nBLOCK\nROOM_QUAD x=0 y=0 w=20 h=16 floor_z=0 ceil_z=6 name=entrance\nSPAWN x=5 y=5 z=1\nMARKER x=5 y=15 z=1 name=door\nCORRIDOR_H w=4 floor_z=0 ceil_z=5\nDOOR w=3 h=4\nROOM_PENT floor_z=0 ceil_z=8 name=arena\nMARKER x=15 y=15 z=1 name=exit\nEBLOCK\n";

    fr_dungeon_layout_t l;
    char json[16384];
    ASSERT_INT_EQ(full_pipeline(input, &l, json, sizeof(json)), 0);

    assert_str_contains(json, "{\"entities\":[");
    assert_str_contains(json, "\"type\":\"room_quad\"");
    assert_str_contains(json, "\"type\":\"room_pent\"");
    assert_str_contains(json, "\"type\":\"corridor\"");
    assert_str_contains(json, "\"type\":\"marker\"");
    assert_str_contains(json, "\"type\":\"spawn\"");
    assert_str_contains(json, "\"name\":\"entrance\"");
    assert_str_contains(json, "\"name\":\"arena\"");
    assert_str_contains(json, "\"name\":\"door\"");
    assert_str_contains(json, "\"name\":\"exit\"");
    assert_str_contains(json, "\"id\":");
    assert_str_contains(json, "\"pos\":");
    assert_str_contains(json, "\"rot\":");
    assert_str_contains(json, "\"scale\":");
    ASSERT_TRUE(json[strlen(json) - 1] == '}');

    free_l(&l);
    PASS();
}

int main(void) {
    printf("=== Procgen P3.5 E2E Tests ===\n\n");
    RUN(test_50_dungeon_strings);
    RUN(test_registry_persists);
    RUN(test_json_integrity);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
