/**
 * @file srd_rules_repair_tests.c
 * @brief Tests for SRD repair rules (Repair Rules 1-5).
 *
 * Non-static functions: 1 (main)
 *
 * Tests:
 *   - Registration count (5 rules)
 *   - All rules have is_repair=true
 *   - All rules have inverse_rule_id=-1
 *   - All rules have name, cond, apply
 *   - ResolveOverlap: overlapping boxes are separated
 *   - RepairContained: enclosed box with no outside neighbours is removed
 *   - RepairContained: enclosed box with outside neighbours is extended
 *   - AlignWall: close-but-not-flush walls are snapped
 *   - ClampToBounds: out-of-bounds box is clamped
 *   - ClampToBounds: oversized box is shrunk
 *   - EnsureConnected: isolated box gains adjacency
 *   - Repair rules excluded from srd_rule_find_applicable
 */
#include "ferrum/procgen/srd/srd_rules_repair.h"
#include "ferrum/procgen/srd/srd_descent_rules.h"
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
    printf("  %-50s ", #name); \
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

#define ASSERT_FLOAT_EQ(a, b, eps) ASSERT(fabsf((a) - (b)) < (eps))

/* ── Helper: make a box ────────────────────────────────────────── */

static srd_sdf_box_t make_box(float cx, float cz, float hw, float hd,
                              srd_room_type_t type) {
    srd_sdf_box_t box;
    memset(&box, 0, sizeof(box));
    box.cx = cx;
    box.cz = cz;
    box.hw = hw;
    box.hd = hd;
    box.type = type;
    return box;
}

/* ── Registration tests ───────────────────────────────────────── */

TEST(test_registration_count) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    int count = srd_rules_repair_register(&tbl);
    ASSERT(count == 5);
    ASSERT(tbl.n_rules == 5);
}

TEST(test_all_rules_are_repair) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT(tbl.rules[i].is_repair == true);
    }
}

TEST(test_all_rules_inverse_minus_one) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT(tbl.rules[i].inverse_rule_id == -1);
    }
}

TEST(test_all_rules_have_name_cond_apply) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);
    for (int i = 0; i < tbl.n_rules; i++) {
        ASSERT(tbl.rules[i].name != NULL);
        ASSERT(tbl.rules[i].cond != NULL);
        ASSERT(tbl.rules[i].apply != NULL);
    }
}

/* ── ResolveOverlap tests ─────────────────────────────────────── */

TEST(test_resolve_overlap_separates_boxes) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    /* Find the ResolveOverlap rule */
    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ResolveOverlap") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    /* Create layout with two overlapping boxes */
    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Box A: centre (5,5) half-extents (3,3) => spans [2,8] x [2,8] */
    srd_sdf_box_t a = make_box(5.0f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);
    /* Box B: centre (7,5) half-extents (3,3) => spans [4,10] x [2,8] */
    srd_sdf_box_t b = make_box(7.0f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);

    int idx_a = srd_sdf_layout_add_box(&layout, &a);
    int idx_b = srd_sdf_layout_add_box(&layout, &b);
    ASSERT(idx_a >= 0 && idx_b >= 0);

    /* Verify they overlap before applying */
    ASSERT(srd_sdf_box_overlap(&layout.boxes[idx_a], &layout.boxes[idx_b]));

    /* Condition should fire */
    srd_selection_t sel = { .indices = {idx_a, idx_b}, .n = 2 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    /* Apply */
    int new_indices[8];
    int result = tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT(result >= 0);

    /* After apply, boxes should no longer overlap */
    ASSERT(!srd_sdf_box_overlap(&layout.boxes[idx_a], &layout.boxes[idx_b]));
}

TEST(test_resolve_overlap_no_overlap_cond_false) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ResolveOverlap") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Non-overlapping boxes */
    srd_sdf_box_t a = make_box(2.0f, 5.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC);
    srd_sdf_box_t b = make_box(10.0f, 5.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC);
    int idx_a = srd_sdf_layout_add_box(&layout, &a);
    int idx_b = srd_sdf_layout_add_box(&layout, &b);

    srd_selection_t sel = { .indices = {idx_a, idx_b}, .n = 2 };
    ASSERT(!tbl.rules[rule_idx].cond(&layout, &sel, NULL));
}

/* ── RepairContained tests ────────────────────────────────────── */

TEST(test_repair_contained_removes_isolated_inner) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RepairContained") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Outer box: centre (10,10), half-extents (8,8) => [2,18] x [2,18] */
    srd_sdf_box_t outer = make_box(10.0f, 10.0f, 8.0f, 8.0f, SRD_ROOM_GENERIC);
    /* Inner box: centre (10,10), half-extents (2,2) => [8,12] x [8,12] — fully inside */
    srd_sdf_box_t inner = make_box(10.0f, 10.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);

    int idx_outer = srd_sdf_layout_add_box(&layout, &outer);
    int idx_inner = srd_sdf_layout_add_box(&layout, &inner);
    ASSERT(idx_outer >= 0 && idx_inner >= 0);

    /* Only adjacency: inner <-> outer (no outside connections for inner) */
    srd_sdf_layout_set_adj(&layout, idx_outer, idx_inner, true);

    ASSERT(srd_sdf_box_contains(&layout.boxes[idx_outer], &layout.boxes[idx_inner]));

    srd_selection_t sel = { .indices = {idx_outer, idx_inner}, .n = 2 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    int n_before = layout.n_boxes;
    int new_indices[8];
    int result = tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT(result >= 0);

    /* Inner box should be removed */
    ASSERT(layout.n_boxes == n_before - 1);
}

TEST(test_repair_contained_extends_connected_inner) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "RepairContained") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Outer: centre (10,10), hw=8, hd=8 => [2,18] x [2,18] */
    srd_sdf_box_t outer = make_box(10.0f, 10.0f, 8.0f, 8.0f, SRD_ROOM_GENERIC);
    /* Inner: centre (10,10), hw=2, hd=2 => [8,12] x [8,12] */
    srd_sdf_box_t inner = make_box(10.0f, 10.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);
    /* Third box outside outer */
    srd_sdf_box_t outside = make_box(30.0f, 10.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);

    int idx_outer = srd_sdf_layout_add_box(&layout, &outer);
    int idx_inner = srd_sdf_layout_add_box(&layout, &inner);
    int idx_outside = srd_sdf_layout_add_box(&layout, &outside);
    ASSERT(idx_outer >= 0 && idx_inner >= 0 && idx_outside >= 0);

    /* Inner is adjacent to both outer and outside */
    srd_sdf_layout_set_adj(&layout, idx_outer, idx_inner, true);
    srd_sdf_layout_set_adj(&layout, idx_inner, idx_outside, true);

    srd_selection_t sel = { .indices = {idx_outer, idx_inner}, .n = 2 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    int n_before = layout.n_boxes;
    int new_indices[8];
    int result = tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT(result >= 0);

    /* Box count should not change (inner extended, not removed) */
    ASSERT(layout.n_boxes == n_before);

    /* Inner should no longer be fully contained */
    ASSERT(!srd_sdf_box_contains(&layout.boxes[idx_outer], &layout.boxes[idx_inner]));
}

/* ── AlignWall tests ──────────────────────────────────────────── */

TEST(test_align_wall_snaps_close_walls) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AlignWall") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Box A: right wall at cx+hw = 5+3 = 8.0 */
    srd_sdf_box_t a = make_box(5.0f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);
    /* Box B: left wall at cx-hw = 8.3-3 = 5.3 — close to A's right wall (diff=0.3) */
    srd_sdf_box_t b = make_box(8.3f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);

    int idx_a = srd_sdf_layout_add_box(&layout, &a);
    int idx_b = srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, idx_a, idx_b, true);

    srd_selection_t sel = { .indices = {idx_a, idx_b}, .n = 2 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    int new_indices[8];
    tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);

    /* After alignment, the gap between walls should be zero or near-zero */
    float right_a = layout.boxes[idx_a].cx + layout.boxes[idx_a].hw;
    float left_b = layout.boxes[idx_b].cx - layout.boxes[idx_b].hw;
    ASSERT_FLOAT_EQ(right_a, left_b, 0.02f);
}

TEST(test_align_wall_not_close_cond_false) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "AlignWall") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Walls are far apart (gap = 5.0) */
    srd_sdf_box_t a = make_box(3.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);
    srd_sdf_box_t b = make_box(15.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);

    int idx_a = srd_sdf_layout_add_box(&layout, &a);
    int idx_b = srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, idx_a, idx_b, true);

    srd_selection_t sel = { .indices = {idx_a, idx_b}, .n = 2 };
    ASSERT(!tbl.rules[rule_idx].cond(&layout, &sel, NULL));
}

/* ── ClampToBounds tests ──────────────────────────────────────── */

TEST(test_clamp_to_bounds_brings_inside) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ClampToBounds") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 20.0f;
    layout.bounds_h = 20.0f;

    /* Box partially outside: centre (-1, 5), hw=3, hd=3 => left edge at -4 */
    srd_sdf_box_t box = make_box(-1.0f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);
    int idx = srd_sdf_layout_add_box(&layout, &box);
    ASSERT(idx >= 0);

    srd_selection_t sel = { .indices = {idx}, .n = 1 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    int new_indices[8];
    tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);

    /* All corners must be within [0, bounds_w] x [0, bounds_h] */
    float left   = layout.boxes[idx].cx - layout.boxes[idx].hw;
    float right  = layout.boxes[idx].cx + layout.boxes[idx].hw;
    float bottom = layout.boxes[idx].cz - layout.boxes[idx].hd;
    float top    = layout.boxes[idx].cz + layout.boxes[idx].hd;

    ASSERT(left >= -0.001f);
    ASSERT(right <= layout.bounds_w + 0.001f);
    ASSERT(bottom >= -0.001f);
    ASSERT(top <= layout.bounds_h + 0.001f);
}

TEST(test_clamp_to_bounds_shrinks_oversized) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ClampToBounds") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 10.0f;
    layout.bounds_h = 10.0f;

    /* Box wider than bounds: hw=8 => width 16 but bounds are 10 */
    srd_sdf_box_t box = make_box(5.0f, 5.0f, 8.0f, 8.0f, SRD_ROOM_GENERIC);
    int idx = srd_sdf_layout_add_box(&layout, &box);
    ASSERT(idx >= 0);

    srd_selection_t sel = { .indices = {idx}, .n = 1 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    int new_indices[8];
    tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);

    /* Box must fit within bounds */
    ASSERT(layout.boxes[idx].hw * 2.0f <= layout.bounds_w + 0.001f);
    ASSERT(layout.boxes[idx].hd * 2.0f <= layout.bounds_h + 0.001f);
    ASSERT(layout.boxes[idx].cx - layout.boxes[idx].hw >= -0.001f);
    ASSERT(layout.boxes[idx].cx + layout.boxes[idx].hw <= layout.bounds_w + 0.001f);
}

TEST(test_clamp_to_bounds_inside_cond_false) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "ClampToBounds") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Box well within bounds */
    srd_sdf_box_t box = make_box(25.0f, 25.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);
    int idx = srd_sdf_layout_add_box(&layout, &box);

    srd_selection_t sel = { .indices = {idx}, .n = 1 };
    ASSERT(!tbl.rules[rule_idx].cond(&layout, &sel, NULL));
}

/* ── EnsureConnected tests ────────────────────────────────────── */

TEST(test_ensure_connected_adds_adjacency) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "EnsureConnected") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Box 0: has a neighbour (box 1) */
    srd_sdf_box_t a = make_box(5.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);
    srd_sdf_box_t b = make_box(12.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);
    /* Box 2: isolated */
    srd_sdf_box_t c = make_box(25.0f, 25.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);

    int idx_a = srd_sdf_layout_add_box(&layout, &a);
    int idx_b = srd_sdf_layout_add_box(&layout, &b);
    int idx_c = srd_sdf_layout_add_box(&layout, &c);
    srd_sdf_layout_set_adj(&layout, idx_a, idx_b, true);

    /* c is isolated */
    ASSERT(srd_sdf_layout_adj_count(&layout, idx_c) == 0);

    srd_selection_t sel = { .indices = {idx_c}, .n = 1 };
    ASSERT(tbl.rules[rule_idx].cond(&layout, &sel, NULL));

    int new_indices[8];
    int result = tbl.rules[rule_idx].apply(&layout, &sel, NULL, new_indices, 8);
    ASSERT(result >= 0);

    /* After apply, box c (which may have shifted index) should have adjacency.
     * A corridor was inserted, so c should have at least 1 neighbour. */
    /* The corridor connects c to nearest box. idx_c might still be valid
     * if corridor was appended. */
    ASSERT(srd_sdf_layout_adj_count(&layout, idx_c) > 0);
}

TEST(test_ensure_connected_not_isolated_cond_false) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    int rule_idx = -1;
    for (int i = 0; i < tbl.n_rules; i++) {
        if (strcmp(tbl.rules[i].name, "EnsureConnected") == 0) {
            rule_idx = i;
            break;
        }
    }
    ASSERT(rule_idx >= 0);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    srd_sdf_box_t a = make_box(5.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);
    srd_sdf_box_t b = make_box(12.0f, 5.0f, 2.0f, 2.0f, SRD_ROOM_GENERIC);
    int idx_a = srd_sdf_layout_add_box(&layout, &a);
    int idx_b = srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, idx_a, idx_b, true);

    /* a has adjacency, so condition should be false */
    srd_selection_t sel = { .indices = {idx_a}, .n = 1 };
    ASSERT(!tbl.rules[rule_idx].cond(&layout, &sel, NULL));
}

/* ── Repair exclusion from find_applicable ─────────────────────── */

TEST(test_repair_rules_excluded_from_find_applicable) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Add overlapping boxes so ResolveOverlap condition would be true */
    srd_sdf_box_t a = make_box(5.0f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);
    srd_sdf_box_t b = make_box(7.0f, 5.0f, 3.0f, 3.0f, SRD_ROOM_GENERIC);
    srd_sdf_layout_add_box(&layout, &a);
    srd_sdf_layout_add_box(&layout, &b);

    /* find_applicable should return 0 since all rules are repair */
    int out[16];
    uint32_t rng = 42;
    int n = srd_rule_find_applicable(&tbl, &layout, out, 16, &rng);
    ASSERT(n == 0);
}

/* ── n_select values ──────────────────────────────────────────── */

TEST(test_rule_n_select_values) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    /* Expected: ResolveOverlap=2, RepairContained=2, AlignWall=2,
     *           ClampToBounds=1, EnsureConnected=1 */
    for (int i = 0; i < tbl.n_rules; i++) {
        const char *name = tbl.rules[i].name;
        if (strcmp(name, "ResolveOverlap") == 0 ||
            strcmp(name, "RepairContained") == 0 ||
            strcmp(name, "AlignWall") == 0) {
            ASSERT(tbl.rules[i].n_select == 2);
        } else if (strcmp(name, "ClampToBounds") == 0 ||
                   strcmp(name, "EnsureConnected") == 0) {
            ASSERT(tbl.rules[i].n_select == 1);
        }
    }
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Repair Rules Tests ===\n");

    RUN(test_registration_count);
    RUN(test_all_rules_are_repair);
    RUN(test_all_rules_inverse_minus_one);
    RUN(test_all_rules_have_name_cond_apply);
    RUN(test_rule_n_select_values);
    RUN(test_resolve_overlap_separates_boxes);
    RUN(test_resolve_overlap_no_overlap_cond_false);
    RUN(test_repair_contained_removes_isolated_inner);
    RUN(test_repair_contained_extends_connected_inner);
    RUN(test_align_wall_snaps_close_walls);
    RUN(test_align_wall_not_close_cond_false);
    RUN(test_clamp_to_bounds_brings_inside);
    RUN(test_clamp_to_bounds_shrinks_oversized);
    RUN(test_clamp_to_bounds_inside_cond_false);
    RUN(test_ensure_connected_adds_adjacency);
    RUN(test_ensure_connected_not_isolated_cond_false);
    RUN(test_repair_rules_excluded_from_find_applicable);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
