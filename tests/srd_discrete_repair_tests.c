/**
 * @file srd_discrete_repair_tests.c
 * @brief Tests for post-rewrite repair rule application.
 *
 * Non-static functions (1): main
 */
#include "ferrum/procgen/srd/srd_discrete_repair.h"
#include "ferrum/procgen/srd/srd_descent_rules.h"
#include "ferrum/procgen/srd/srd_rules_repair.h"
#include "ferrum/procgen/srd/srd_sdf_layout.h"
#include "ferrum/procgen/srd/srd_room_type.h"

#include <math.h>
#include <stdio.h>
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

/* ── Tests ────────────────────────────────────────────────────── */

TEST(test_no_box_outside_bounds) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 20.0f;
    layout.bounds_h = 20.0f;

    /* Box outside bounds */
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.cx = -5.0f; b.cz = 10.0f; b.hw = 3.0f; b.hd = 3.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&layout, &b);

    b.cx = 10.0f; b.cz = 10.0f; b.hw = 2.0f; b.hd = 2.0f;
    srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, 0, 1, true);

    srd_apply_repairs(&layout, &tbl);

    /* All boxes should be within bounds */
    for (int i = 0; i < layout.n_boxes; i++) {
        float left   = layout.boxes[i].cx - layout.boxes[i].hw;
        float right  = layout.boxes[i].cx + layout.boxes[i].hw;
        float bottom = layout.boxes[i].cz - layout.boxes[i].hd;
        float top    = layout.boxes[i].cz + layout.boxes[i].hd;
        ASSERT(left >= -0.01f);
        ASSERT(right <= layout.bounds_w + 0.01f);
        ASSERT(bottom >= -0.01f);
        ASSERT(top <= layout.bounds_h + 0.01f);
    }
}

TEST(test_no_overlap_after_repair) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Two overlapping boxes */
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.cx = 10.0f; b.cz = 10.0f; b.hw = 5.0f; b.hd = 5.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&layout, &b);

    b.cx = 12.0f; b.cz = 10.0f; b.hw = 5.0f; b.hd = 5.0f;
    srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, 0, 1, true);

    srd_apply_repairs(&layout, &tbl);

    /* No pair should overlap */
    for (int i = 0; i < layout.n_boxes; i++) {
        for (int j = i + 1; j < layout.n_boxes; j++) {
            ASSERT(!srd_sdf_box_overlap(&layout.boxes[i], &layout.boxes[j]));
        }
    }
}

TEST(test_no_isolated_box_after_repair) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 50.0f;
    layout.bounds_h = 50.0f;

    /* Three boxes, one isolated */
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.cx = 5.0f; b.cz = 5.0f; b.hw = 2.0f; b.hd = 2.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&layout, &b);

    b.cx = 15.0f; b.cz = 5.0f;
    srd_sdf_layout_add_box(&layout, &b);

    b.cx = 40.0f; b.cz = 40.0f; /* isolated */
    srd_sdf_layout_add_box(&layout, &b);

    srd_sdf_layout_set_adj(&layout, 0, 1, true);

    srd_apply_repairs(&layout, &tbl);

    /* All boxes should have >= 1 adjacency (corridor may have been added) */
    for (int i = 0; i < layout.n_boxes; i++) {
        ASSERT(srd_sdf_layout_adj_count(&layout, i) > 0);
    }
}

TEST(test_idempotent) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    srd_rules_repair_register(&tbl);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);
    layout.bounds_w = 30.0f;
    layout.bounds_h = 30.0f;

    /* A clean layout — no violations */
    srd_sdf_box_t b;
    memset(&b, 0, sizeof(b));
    b.cx = 5.0f; b.cz = 5.0f; b.hw = 2.0f; b.hd = 2.0f;
    b.type = SRD_ROOM_GENERIC;
    srd_sdf_layout_add_box(&layout, &b);

    b.cx = 15.0f; b.cz = 5.0f;
    srd_sdf_layout_add_box(&layout, &b);
    srd_sdf_layout_set_adj(&layout, 0, 1, true);

    /* First pass */
    int fired1 = srd_apply_repairs(&layout, &tbl);

    /* Second pass should fire nothing */
    int fired2 = srd_apply_repairs(&layout, &tbl);
    ASSERT(fired2 == 0);
}

/* ── Main ─────────────────────────────────────────────────────── */

int main(void) {
    printf("=== SRD Discrete Repair Tests ===\n");

    RUN(test_no_box_outside_bounds);
    RUN(test_no_overlap_after_repair);
    RUN(test_no_isolated_box_after_repair);
    RUN(test_idempotent);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
