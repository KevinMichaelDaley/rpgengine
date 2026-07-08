/**
 * @file srd_discrete_maxcover_tests.c
 * @brief Tests for compatibility graph and greedy max-cover.
 *
 * Non-static functions (1): main
 */
#include "ferrum/procgen/srd/srd_discrete_maxcover.h"
#include "ferrum/procgen/srd/srd_discrete_candidates.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ── Test harness ──────────────────────────────────────────────── */

static int g_pass = 0;
static int g_fail = 0;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-55s ", #name); \
    name(); \
    printf("[PASS]\n"); \
    g_pass++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("[FAIL] %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail++; return; \
    } \
} while (0)

/* ── Helper: build a simple candidate ────────────────────────── */

static srd_candidate_t make_candidate(int rule_idx, int box_idx, float delta_L) {
    srd_candidate_t c;
    memset(&c, 0, sizeof(c));
    c.rule_idx = rule_idx;
    c.sel.indices[0] = box_idx;
    c.sel.n = 1;
    c.delta_L = delta_L;
    /* Set layout_copy with a box at different positions per box_idx */
    srd_sdf_layout_init(&c.layout_copy);
    c.layout_copy.bounds_w = 100.0f;
    c.layout_copy.bounds_h = 100.0f;
    srd_sdf_box_t box;
    memset(&box, 0, sizeof(box));
    box.cx = (float)(box_idx * 20);
    box.cz = (float)(box_idx * 20);
    box.hw = 3.0f;
    box.hd = 3.0f;
    box.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&c.layout_copy, &box);
    return c;
}

/* ── Tests ────────────────────────────────────────────────────── */

TEST(test_same_box_incompatible) {
    /* Two candidates touching the same box should be incompatible */
    srd_candidate_t cands[2];
    cands[0] = make_candidate(0, 0, 1.0f); /* box 0 */
    cands[1] = make_candidate(1, 0, 0.5f); /* box 0 */

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    /* Register dummy rules with small locality_radius */
    srd_descent_rule_t r = {0};
    r.name = "DummyA"; r.inverse_rule_id = -1; r.n_select = 1;
    r.locality_radius = 5.0f;
    srd_rule_table_register(&tbl, &r);
    r.name = "DummyB";
    srd_rule_table_register(&tbl, &r);

    uint8_t compat[4]; /* 2x2 */
    srd_build_compatibility(&tbl, cands, 2, &cands[0].layout_copy, compat, 2);

    /* Same box => incompatible */
    ASSERT(compat[0 * 2 + 1] == 0);
    ASSERT(compat[1 * 2 + 0] == 0);
}

TEST(test_distant_boxes_compatible) {
    /* Two candidates touching distant boxes should be compatible */
    srd_candidate_t cands[2];
    cands[0] = make_candidate(0, 0, 1.0f); /* box at index 0 */
    cands[1] = make_candidate(1, 1, 0.5f); /* box at index 1 */

    /* Build a layout with both boxes far apart */
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 100.0f;
    layout.bounds_h = 100.0f;
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.hw = 3.0f; b.hd = 3.0f; b.type = SRD_ROOM_GENERIC;
    b.cx = 5.0f; b.cz = 5.0f;
    srd_sdf_layout_add_box(&layout, &b);
    b.cx = 80.0f; b.cz = 80.0f;
    srd_sdf_layout_add_box(&layout, &b);

    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_descent_rule_t r = {0};
    r.name = "DummyA"; r.inverse_rule_id = -1; r.n_select = 1;
    r.locality_radius = 10.0f;
    srd_rule_table_register(&tbl, &r);
    r.name = "DummyB";
    srd_rule_table_register(&tbl, &r);

    uint8_t compat[4];
    srd_build_compatibility(&tbl, cands, 2, &layout, compat, 2);

    /* Distant => compatible */
    ASSERT(compat[0 * 2 + 1] == 1);
    ASSERT(compat[1 * 2 + 0] == 1);
}

TEST(test_greedy_selects_at_least_one) {
    srd_candidate_t *cands = (srd_candidate_t *)calloc(3, sizeof(srd_candidate_t));
    cands[0] = make_candidate(0, 0, 2.0f);
    cands[1] = make_candidate(1, 1, 1.0f);
    cands[2] = make_candidate(2, 2, 0.5f);

    /* All compatible (different boxes, distant) */
    uint8_t compat[9];
    memset(compat, 1, sizeof(compat));
    for (int i = 0; i < 3; i++) compat[i * 3 + i] = 0; /* diagonal */

    int selected[3];
    int n = srd_greedy_max_cover(cands, 3, compat, 3, selected, 3);

    ASSERT(n >= 1);
    free(cands);
}

TEST(test_all_selected_pairwise_compatible) {
    srd_candidate_t *cands = (srd_candidate_t *)calloc(4, sizeof(srd_candidate_t));
    cands[0] = make_candidate(0, 0, 3.0f);
    cands[1] = make_candidate(1, 0, 2.0f); /* conflicts with 0 */
    cands[2] = make_candidate(2, 2, 1.5f);
    cands[3] = make_candidate(3, 3, 1.0f);

    /* Build compatibility: 0 and 1 conflict, others compatible */
    uint8_t compat[16];
    memset(compat, 1, sizeof(compat));
    for (int i = 0; i < 4; i++) compat[i * 4 + i] = 0;
    compat[0 * 4 + 1] = 0; compat[1 * 4 + 0] = 0; /* 0-1 conflict */

    int selected[4];
    int n = srd_greedy_max_cover(cands, 4, compat, 4, selected, 4);

    /* Verify pairwise compatibility of selected set */
    for (int i = 0; i < n; i++) {
        for (int j = i + 1; j < n; j++) {
            int a = selected[i], b = selected[j];
            ASSERT(compat[a * 4 + b] == 1);
        }
    }

    /* 0 should be selected (highest delta_L), 1 should not */
    bool has_0 = false, has_1 = false;
    for (int i = 0; i < n; i++) {
        if (selected[i] == 0) has_0 = true;
        if (selected[i] == 1) has_1 = true;
    }
    ASSERT(has_0);
    ASSERT(!has_1);

    free(cands);
}

TEST(test_total_delta_L_at_least_best_single) {
    srd_candidate_t *cands = (srd_candidate_t *)calloc(3, sizeof(srd_candidate_t));
    cands[0] = make_candidate(0, 0, 5.0f);
    cands[1] = make_candidate(1, 1, 3.0f);
    cands[2] = make_candidate(2, 2, 1.0f);

    /* All compatible */
    uint8_t compat[9];
    memset(compat, 1, sizeof(compat));
    for (int i = 0; i < 3; i++) compat[i * 3 + i] = 0;

    int selected[3];
    int n = srd_greedy_max_cover(cands, 3, compat, 3, selected, 3);

    float total = 0.0f;
    for (int i = 0; i < n; i++) total += cands[selected[i]].delta_L;
    ASSERT(total >= 5.0f); /* at least the best single */

    free(cands);
}

TEST(test_negative_delta_L_excluded) {
    srd_candidate_t *cands = (srd_candidate_t *)calloc(3, sizeof(srd_candidate_t));
    cands[0] = make_candidate(0, 0, -1.0f); /* negative */
    cands[1] = make_candidate(1, 1, -0.5f); /* negative */
    cands[2] = make_candidate(2, 2, 2.0f);  /* positive */

    uint8_t compat[9];
    memset(compat, 1, sizeof(compat));
    for (int i = 0; i < 3; i++) compat[i * 3 + i] = 0;

    int selected[3];
    int n = srd_greedy_max_cover(cands, 3, compat, 3, selected, 3);

    /* Only candidate 2 should be selected */
    ASSERT(n == 1);
    ASSERT(selected[0] == 2);

    free(cands);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Discrete Max-Cover Tests ===\n");

    RUN(test_same_box_incompatible);
    RUN(test_distant_boxes_compatible);
    RUN(test_greedy_selects_at_least_one);
    RUN(test_all_selected_pairwise_compatible);
    RUN(test_total_delta_L_at_least_best_single);
    RUN(test_negative_delta_L_excluded);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
