/**
 * @file viewport_bsp_tests.c
 * @brief Tests for BSP-based multi-viewport tiling.
 */

#include <stdio.h>
#include <string.h>
#include "ferrum/editor/scene/viewport_bsp/viewport_bsp.h"
#include "ferrum/editor/scene/scene_panel.h"

/* ---- Test harness ---- */

static int g_pass, g_fail;

#define RUN(fn) do { \
    printf("RUN  %s\n", #fn); \
    if (fn()) { printf("OK   %s\n", #fn); g_pass++; } \
    else       { printf("FAIL %s\n", #fn); g_fail++; } \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("  ASSERT FAILED: %s (line %d)\n", #cond, __LINE__); \
        return false; \
    } \
} while (0)

#define ASSERT_EQ(a, b) do { \
    if ((a) != (b)) { \
        printf("  ASSERT_EQ FAILED: %s == %d, expected %d (line %d)\n", \
               #a, (int)(a), (int)(b), __LINE__); \
        return false; \
    } \
} while (0)

/* ---- Happy path tests ---- */

/** After init, tree has 1 leaf at root. */
static bool test_init_single_viewport(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);
    ASSERT_EQ(bsp.viewport_count, 1);
    ASSERT_EQ(bsp.focused_viewport, 0);
    ASSERT(bsp.nodes[0].active);
    ASSERT_EQ(bsp.nodes[0].split, SPLIT_NONE);
    ASSERT_EQ(bsp.nodes[0].viewport_index, 0);
    ASSERT_EQ(viewport_bsp_leaf_count(&bsp), 1);
    return true;
}

/** Split root vertically produces 2 leaves. */
static bool test_split_vertical(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp = 255;
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp));

    ASSERT_EQ(bsp.viewport_count, 2);
    ASSERT_EQ(bsp.nodes[0].split, SPLIT_VERTICAL);
    ASSERT(bsp.nodes[0].active);

    /* Children are at indices 1 and 2. */
    ASSERT(bsp.nodes[1].active);
    ASSERT_EQ(bsp.nodes[1].split, SPLIT_NONE);
    ASSERT(bsp.nodes[2].active);
    ASSERT_EQ(bsp.nodes[2].split, SPLIT_NONE);

    /* Original (viewport 0) should be in first child (left). */
    ASSERT_EQ(bsp.nodes[1].viewport_index, 0);
    /* New viewport should be in second child (right). */
    ASSERT_EQ(bsp.nodes[2].viewport_index, new_vp);
    ASSERT(new_vp != 0);

    return true;
}

/** Split root horizontally produces 2 leaves. */
static bool test_split_horizontal(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp = 255;
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_HORIZONTAL, true, &new_vp));

    ASSERT_EQ(bsp.viewport_count, 2);
    ASSERT_EQ(bsp.nodes[0].split, SPLIT_HORIZONTAL);
    ASSERT(bsp.nodes[1].active);
    ASSERT(bsp.nodes[2].active);

    return true;
}

/** Vertical split at ratio 0.5 of 800x600 gives two 400x600 rects. */
static bool test_compute_rects(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    panel_rect_t panel = {100, 50, 800, 600};
    panel_rect_t rects[VIEWPORT_MAX_COUNT];
    memset(rects, 0, sizeof(rects));
    viewport_bsp_compute_rects(&bsp, &panel, rects);

    /* Left child (viewport 0): x=100, w=399 (1px gap on right). */
    ASSERT_EQ(rects[0].x, 100);
    ASSERT_EQ(rects[0].y, 50);
    ASSERT_EQ(rects[0].w, 399);
    ASSERT_EQ(rects[0].h, 600);

    /* Right child (new_vp): x=501, w=399 (1px gap on left). */
    ASSERT_EQ(rects[new_vp].x, 501);
    ASSERT_EQ(rects[new_vp].y, 50);
    ASSERT_EQ(rects[new_vp].w, 399);
    ASSERT_EQ(rects[new_vp].h, 600);

    return true;
}

/** Nested splits: vertical at root, then horizontal on left child. */
static bool test_compute_rects_nested(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t vp1, vp2;
    /* Split root vertical: vp0 left, vp1 right. */
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &vp1);
    /* Split left child horizontal: vp0 top, vp2 bottom. */
    viewport_bsp_split(&bsp, 0, SPLIT_HORIZONTAL, true, &vp2);

    ASSERT_EQ(bsp.viewport_count, 3);

    panel_rect_t panel = {0, 0, 800, 600};
    panel_rect_t rects[VIEWPORT_MAX_COUNT];
    memset(rects, 0, sizeof(rects));
    viewport_bsp_compute_rects(&bsp, &panel, rects);

    /* vp0: top-left quadrant (399x299 with gaps). */
    ASSERT_EQ(rects[0].x, 0);
    ASSERT_EQ(rects[0].y, 0);
    ASSERT_EQ(rects[0].w, 399);
    ASSERT_EQ(rects[0].h, 299);

    /* vp2: bottom-left quadrant (399x299 with gaps). */
    ASSERT_EQ(rects[vp2].x, 0);
    ASSERT_EQ(rects[vp2].y, 301);
    ASSERT_EQ(rects[vp2].w, 399);
    ASSERT_EQ(rects[vp2].h, 299);

    /* vp1: right half (399x600 with gap). */
    ASSERT_EQ(rects[vp1].x, 401);
    ASSERT_EQ(rects[vp1].y, 0);
    ASSERT_EQ(rects[vp1].w, 399);
    ASSERT_EQ(rects[vp1].h, 600);

    return true;
}

/** Close one leaf after split: parent reverts to leaf. */
static bool test_close_leaf(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);
    ASSERT_EQ(bsp.viewport_count, 2);

    /* Close the new viewport; original should remain. */
    ASSERT(viewport_bsp_close(&bsp, new_vp));
    ASSERT_EQ(bsp.viewport_count, 1);
    ASSERT_EQ(bsp.nodes[0].split, SPLIT_NONE);
    ASSERT_EQ(bsp.nodes[0].viewport_index, 0);
    ASSERT(bsp.nodes[0].active);

    return true;
}

/** Hit-test: point in left half hits left viewport. */
static bool test_hit_test_viewport(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    panel_rect_t panel = {0, 0, 800, 600};
    panel_rect_t rects[VIEWPORT_MAX_COUNT];
    memset(rects, 0, sizeof(rects));
    viewport_bsp_compute_rects(&bsp, &panel, rects);

    uint8_t hit = 255;
    /* Point in left half. */
    ASSERT(viewport_bsp_hit_test_viewport(&bsp, rects, 100, 300, &hit));
    ASSERT_EQ(hit, 0);

    /* Point in right half. */
    ASSERT(viewport_bsp_hit_test_viewport(&bsp, rects, 600, 300, &hit));
    ASSERT_EQ(hit, new_vp);

    return true;
}

/** Hit-test: point near divider line returns correct node. */
static bool test_hit_test_divider(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    panel_rect_t panel = {0, 0, 800, 600};
    uint8_t node = 255;

    /* Point at x=400 (the divider) should hit the root node. */
    ASSERT(viewport_bsp_hit_test_divider(&bsp, &panel, 400, 300, &node));
    ASSERT_EQ(node, 0);

    /* Point far from divider should miss. */
    ASSERT(!viewport_bsp_hit_test_divider(&bsp, &panel, 100, 300, &node));

    return true;
}

/** Drag divider adjusts ratio. */
static bool test_drag_divider(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    /* Initial ratio should be 0.5. */
    float initial_ratio = bsp.nodes[0].ratio;
    ASSERT(initial_ratio > 0.49f && initial_ratio < 0.51f);

    /* Drag +80px in an 800-wide rect. */
    viewport_bsp_drag_divider(&bsp, 0, 80, 800);
    float new_ratio = bsp.nodes[0].ratio;
    /* 0.5 + 80/800 = 0.6 */
    ASSERT(new_ratio > 0.59f && new_ratio < 0.61f);

    return true;
}

/** Alt+Left: original viewport in left (first) child. */
static bool test_split_direction_alt_left(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    /* Alt+Left: vertical split, original_first=true (goes left). */
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp));
    ASSERT_EQ(bsp.nodes[1].viewport_index, 0);  /* Original left. */
    ASSERT_EQ(bsp.nodes[2].viewport_index, new_vp); /* New right. */

    return true;
}

/** Alt+Right: original viewport in right (second) child. */
static bool test_split_direction_alt_right(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    /* Alt+Right: vertical split, original_first=false (goes right). */
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, false, &new_vp));
    ASSERT_EQ(bsp.nodes[2].viewport_index, 0);  /* Original right. */
    ASSERT_EQ(bsp.nodes[1].viewport_index, new_vp); /* New left. */

    return true;
}

/* ---- Edge case tests ---- */

/** Split until VIEWPORT_MAX_COUNT; next split fails. */
static bool test_max_splits(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    /* Split repeatedly until we hit max. */
    for (int i = 1; i < VIEWPORT_MAX_COUNT; i++) {
        uint8_t new_vp;
        /* Find any splittable viewport. */
        bool found = false;
        for (uint8_t v = 0; v < VIEWPORT_MAX_COUNT; v++) {
            if (viewport_bsp_can_split(&bsp, v)) {
                ASSERT(viewport_bsp_split(&bsp, v, SPLIT_VERTICAL,
                                          true, &new_vp));
                found = true;
                break;
            }
        }
        ASSERT(found);
    }

    ASSERT_EQ(bsp.viewport_count, VIEWPORT_MAX_COUNT);

    /* One more split should fail. */
    uint8_t new_vp;
    for (uint8_t v = 0; v < VIEWPORT_MAX_COUNT; v++) {
        ASSERT(!viewport_bsp_can_split(&bsp, v));
    }
    /* Try to split anyway. */
    ASSERT(!viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp));

    return true;
}

/** Cannot close the last remaining viewport. */
static bool test_close_last_viewport(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);
    ASSERT(!viewport_bsp_close(&bsp, 0));
    ASSERT_EQ(bsp.viewport_count, 1);
    return true;
}

/** Drag divider clamps to minimum size. */
static bool test_drag_divider_clamp_min(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    /* Drag far to the left — should clamp so left child >= VIEWPORT_MIN_SIZE. */
    viewport_bsp_drag_divider(&bsp, 0, -9999, 800);
    float min_ratio = (float)VIEWPORT_MIN_SIZE / 800.0f;
    ASSERT(bsp.nodes[0].ratio >= min_ratio - 0.001f);

    return true;
}

/** Drag divider clamps to maximum. */
static bool test_drag_divider_clamp_max(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    /* Drag far to the right — should clamp so right child >= VIEWPORT_MIN_SIZE. */
    viewport_bsp_drag_divider(&bsp, 0, 9999, 800);
    float max_ratio = 1.0f - (float)VIEWPORT_MIN_SIZE / 800.0f;
    ASSERT(bsp.nodes[0].ratio <= max_ratio + 0.001f);

    return true;
}

/** Zero-dimension parent rect produces zero rects without crashing. */
static bool test_compute_rects_zero_area(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    panel_rect_t panel = {0, 0, 0, 0};
    panel_rect_t rects[VIEWPORT_MAX_COUNT];
    memset(rects, 0, sizeof(rects));
    viewport_bsp_compute_rects(&bsp, &panel, rects);

    ASSERT_EQ(rects[0].w, 0);
    ASSERT_EQ(rects[0].h, 0);

    return true;
}

/** find_leaf returns correct node index. */
static bool test_find_leaf_by_viewport_index(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    /* Single viewport: leaf is at node 0. */
    ASSERT_EQ(viewport_bsp_find_leaf(&bsp, 0), 0);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp);

    /* vp0 is now at node 1. */
    ASSERT_EQ(viewport_bsp_find_leaf(&bsp, 0), 1);
    /* new_vp is at node 2. */
    ASSERT_EQ(viewport_bsp_find_leaf(&bsp, new_vp), 2);

    /* Non-existent viewport. */
    ASSERT_EQ(viewport_bsp_find_leaf(&bsp, 7), VIEWPORT_BSP_MAX_NODES);

    return true;
}

/* ---- Failure mode tests ---- */

/** Hit test with point outside all rects returns no hit. */
static bool test_hit_test_outside(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    panel_rect_t panel = {100, 100, 400, 300};
    panel_rect_t rects[VIEWPORT_MAX_COUNT];
    memset(rects, 0, sizeof(rects));
    viewport_bsp_compute_rects(&bsp, &panel, rects);

    uint8_t hit = 255;
    /* Point way outside. */
    ASSERT(!viewport_bsp_hit_test_viewport(&bsp, rects, 0, 0, &hit));

    return true;
}

/** Close with invalid viewport index returns false. */
static bool test_close_invalid_index(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);
    ASSERT(!viewport_bsp_close(&bsp, 99));
    return true;
}

/** Close then re-split reuses viewport slots. */
static bool test_close_and_resplit(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t vp1;
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &vp1));
    ASSERT_EQ(bsp.viewport_count, 2);

    ASSERT(viewport_bsp_close(&bsp, vp1));
    ASSERT_EQ(bsp.viewport_count, 1);

    /* Re-split should work fine. */
    uint8_t vp2;
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_HORIZONTAL, false, &vp2));
    ASSERT_EQ(bsp.viewport_count, 2);

    return true;
}

/** Close original viewport (not the new one) promotes sibling. */
static bool test_close_original(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    ASSERT(viewport_bsp_split(&bsp, 0, SPLIT_VERTICAL, true, &new_vp));

    /* Close the original viewport (0). */
    ASSERT(viewport_bsp_close(&bsp, 0));
    ASSERT_EQ(bsp.viewport_count, 1);

    /* Root should now be a leaf with the surviving viewport. */
    ASSERT_EQ(bsp.nodes[0].split, SPLIT_NONE);
    ASSERT_EQ(bsp.nodes[0].viewport_index, new_vp);
    ASSERT(bsp.nodes[0].active);

    return true;
}

/** Horizontal split hit test for divider. */
static bool test_hit_test_divider_horizontal(void) {
    viewport_bsp_t bsp;
    viewport_bsp_init(&bsp);

    uint8_t new_vp;
    viewport_bsp_split(&bsp, 0, SPLIT_HORIZONTAL, true, &new_vp);

    panel_rect_t panel = {0, 0, 800, 600};
    uint8_t node = 255;

    /* Horizontal split at y=300 — point near should hit. */
    ASSERT(viewport_bsp_hit_test_divider(&bsp, &panel, 400, 300, &node));
    ASSERT_EQ(node, 0);

    /* Point far from divider should miss. */
    ASSERT(!viewport_bsp_hit_test_divider(&bsp, &panel, 400, 100, &node));

    return true;
}

/* ---- Main ---- */

int main(void) {
    g_pass = g_fail = 0;

    /* Happy path. */
    RUN(test_init_single_viewport);
    RUN(test_split_vertical);
    RUN(test_split_horizontal);
    RUN(test_compute_rects);
    RUN(test_compute_rects_nested);
    RUN(test_close_leaf);
    RUN(test_hit_test_viewport);
    RUN(test_hit_test_divider);
    RUN(test_drag_divider);
    RUN(test_split_direction_alt_left);
    RUN(test_split_direction_alt_right);

    /* Edge cases. */
    RUN(test_max_splits);
    RUN(test_close_last_viewport);
    RUN(test_drag_divider_clamp_min);
    RUN(test_drag_divider_clamp_max);
    RUN(test_compute_rects_zero_area);
    RUN(test_find_leaf_by_viewport_index);

    /* Failure modes. */
    RUN(test_hit_test_outside);
    RUN(test_close_invalid_index);
    RUN(test_close_and_resplit);
    RUN(test_close_original);
    RUN(test_hit_test_divider_horizontal);

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
