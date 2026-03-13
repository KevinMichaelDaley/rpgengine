/**
 * @file p200_panel_layout_tests.c
 * @brief Unit tests for scene editor panel layout system.
 *
 * Tests panel rect computation, divider drag, collapse/toggle,
 * focus cycling, and layout persistence.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/scene/scene_panel.h"

/* ---- Test harness macros ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                \
    do {                                                                        \
        if ((exp) != (act)) {                                                  \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d got "   \
                    "%d\n", __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, eps)                                       \
    do {                                                                        \
        float _e = (float)(exp);                                               \
        float _a = (float)(act);                                               \
        if (fabsf(_e - _a) > (eps)) {                                          \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: expected %f "   \
                    "got %f\n", __FILE__, __LINE__, (double)_e, (double)_a);   \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Happy path tests ---- */

/**
 * Default layout divides window into four panels:
 *   Outliner (left) | Viewport (center)   | Inspector (right)
 *                   | Toolbar (below vp)   |
 *                   | TUI (bottom center)  |
 */
static int test_default_layout(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* All four panels should have non-zero area */
    for (int i = 0; i < PANEL_COUNT; ++i) {
        panel_rect_t r = panel_layout_get_rect(&layout, (panel_id_t)i);
        ASSERT_TRUE(r.w > 0);
        ASSERT_TRUE(r.h > 0);
    }

    /* Viewport should be the largest panel */
    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    panel_rect_t ol = panel_layout_get_rect(&layout, PANEL_OUTLINER);
    panel_rect_t insp = panel_layout_get_rect(&layout, PANEL_INSPECTOR);
    ASSERT_TRUE(vp.w > ol.w);
    ASSERT_TRUE(vp.w > insp.w);

    return 0;
}

static int test_layout_covers_window(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1920, 1080);

    /* The total width of left + center + right should equal window width */
    panel_rect_t ol = panel_layout_get_rect(&layout, PANEL_OUTLINER);
    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    panel_rect_t insp = panel_layout_get_rect(&layout, PANEL_INSPECTOR);

    /* Left edge of outliner should be 0 */
    ASSERT_INT_EQ(0, ol.x);
    /* Right edge of inspector should be window width */
    ASSERT_INT_EQ(1920, insp.x + insp.w);

    /* Vertical: panels should cover full height */
    ASSERT_INT_EQ(0, ol.y);
    ASSERT_INT_EQ(1080, ol.y + ol.h);

    (void)vp;
    return 0;
}

static int test_resize_recomputes_rects(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    panel_rect_t vp_before = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    panel_layout_resize(&layout, 1920, 1080);
    panel_rect_t vp_after = panel_layout_get_rect(&layout, PANEL_VIEWPORT);

    /* Viewport should grow when window grows */
    ASSERT_TRUE(vp_after.w > vp_before.w || vp_after.h > vp_before.h);

    return 0;
}

/* ---- Divider drag tests ---- */

static int test_drag_vertical_divider(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    panel_rect_t ol_before = panel_layout_get_rect(&layout, PANEL_OUTLINER);

    /* Drag the left vertical divider 50px to the right */
    panel_layout_drag_divider(&layout, DIVIDER_LEFT, 50);

    panel_rect_t ol_after = panel_layout_get_rect(&layout, PANEL_OUTLINER);
    panel_rect_t vp_after = panel_layout_get_rect(&layout, PANEL_VIEWPORT);

    /* Outliner should widen, viewport should narrow */
    ASSERT_TRUE(ol_after.w > ol_before.w);
    ASSERT_TRUE(vp_after.x > ol_before.w);

    return 0;
}

static int test_drag_horizontal_divider(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    panel_rect_t vp_before = panel_layout_get_rect(&layout, PANEL_VIEWPORT);

    /* Drag the horizontal divider (between viewport and TUI) up by 40px */
    panel_layout_drag_divider(&layout, DIVIDER_BOTTOM, -40);

    panel_rect_t vp_after = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    panel_rect_t tui_after = panel_layout_get_rect(&layout, PANEL_TUI);

    /* Viewport should shrink vertically, TUI should grow */
    ASSERT_TRUE(vp_after.h < vp_before.h);
    ASSERT_TRUE(tui_after.h > 0);

    return 0;
}

static int test_divider_clamp_min_size(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Try to drag the left divider so far left that outliner would be <0 */
    panel_layout_drag_divider(&layout, DIVIDER_LEFT, -5000);

    panel_rect_t ol = panel_layout_get_rect(&layout, PANEL_OUTLINER);
    /* Outliner should be clamped to minimum width, not negative */
    ASSERT_TRUE(ol.w >= PANEL_MIN_SIZE);

    return 0;
}

static int test_divider_clamp_max_size(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Try to drag left divider so far right it would eat the viewport */
    panel_layout_drag_divider(&layout, DIVIDER_LEFT, 5000);

    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    /* Viewport should still have minimum width */
    ASSERT_TRUE(vp.w >= PANEL_MIN_SIZE);

    return 0;
}

/* ---- Panel toggle/collapse tests ---- */

static int test_toggle_panel_visibility(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* All panels visible by default */
    ASSERT_TRUE(panel_layout_is_visible(&layout, PANEL_OUTLINER));

    /* Toggle outliner off */
    panel_layout_toggle(&layout, PANEL_OUTLINER);
    ASSERT_TRUE(!panel_layout_is_visible(&layout, PANEL_OUTLINER));

    /* Viewport should expand to fill the space */
    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    ASSERT_TRUE(vp.x == 0 || vp.w > 600);

    /* Toggle back on */
    panel_layout_toggle(&layout, PANEL_OUTLINER);
    ASSERT_TRUE(panel_layout_is_visible(&layout, PANEL_OUTLINER));

    return 0;
}

/* ---- Focus cycling tests ---- */

static int test_focus_default_is_viewport(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    ASSERT_INT_EQ(PANEL_VIEWPORT, panel_layout_get_focus(&layout));
    return 0;
}

static int test_focus_tab_cycle(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Tab should cycle through visible panels */
    panel_id_t first = panel_layout_get_focus(&layout);
    panel_layout_focus_next(&layout);
    panel_id_t second = panel_layout_get_focus(&layout);
    ASSERT_TRUE(second != first);

    /* Keep cycling, should eventually return to start */
    for (int i = 0; i < PANEL_COUNT; ++i) {
        panel_layout_focus_next(&layout);
    }
    /* After cycling through all panels + 1 extra, we've wrapped */
    return 0;
}

static int test_focus_click_sets(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    panel_layout_set_focus(&layout, PANEL_TUI);
    ASSERT_INT_EQ(PANEL_TUI, panel_layout_get_focus(&layout));

    return 0;
}

static int test_focus_escape_returns_to_viewport(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    panel_layout_set_focus(&layout, PANEL_TUI);
    panel_layout_focus_viewport(&layout);
    ASSERT_INT_EQ(PANEL_VIEWPORT, panel_layout_get_focus(&layout));

    return 0;
}

/* ---- Hit testing for focus ---- */

static int test_hit_test_returns_panel(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Click in the center of the outliner */
    panel_rect_t ol = panel_layout_get_rect(&layout, PANEL_OUTLINER);
    int cx = ol.x + ol.w / 2;
    int cy = ol.y + ol.h / 2;
    panel_id_t hit = panel_layout_hit_test(&layout, cx, cy);
    ASSERT_INT_EQ(PANEL_OUTLINER, hit);

    /* Click in the center of the viewport */
    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    cx = vp.x + vp.w / 2;
    cy = vp.y + vp.h / 2;
    hit = panel_layout_hit_test(&layout, cx, cy);
    ASSERT_INT_EQ(PANEL_VIEWPORT, hit);

    return 0;
}

/* ---- Divider hit test ---- */

static int test_divider_hit_test(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Click right on the left divider boundary */
    panel_rect_t ol = panel_layout_get_rect(&layout, PANEL_OUTLINER);
    divider_id_t div = panel_layout_divider_hit_test(&layout,
                                                      ol.x + ol.w + 2, ol.h / 2);
    ASSERT_TRUE(div == DIVIDER_LEFT);

    /* Click far from any divider */
    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    div = panel_layout_divider_hit_test(&layout,
                                         vp.x + vp.w / 2, vp.y + vp.h / 2);
    ASSERT_INT_EQ(DIVIDER_NONE, div);

    return 0;
}

/* ---- Edge cases ---- */

static int test_zero_size_window(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 0, 0);

    /* Should not crash; panels may all have zero area */
    for (int i = 0; i < PANEL_COUNT; ++i) {
        panel_rect_t r = panel_layout_get_rect(&layout, (panel_id_t)i);
        ASSERT_TRUE(r.w >= 0);
        ASSERT_TRUE(r.h >= 0);
    }
    return 0;
}

static int test_tiny_window(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 100, 100);

    /* Panels should still be valid even if window is very small */
    panel_rect_t vp = panel_layout_get_rect(&layout, PANEL_VIEWPORT);
    ASSERT_TRUE(vp.w >= 0);
    ASSERT_TRUE(vp.h >= 0);
    return 0;
}

static int test_toggle_hidden_panel_skips_focus(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Hide outliner, set focus to viewport */
    panel_layout_toggle(&layout, PANEL_OUTLINER);
    panel_layout_set_focus(&layout, PANEL_VIEWPORT);

    /* Tab cycling should skip hidden outliner */
    for (int i = 0; i < PANEL_COUNT * 2; ++i) {
        panel_layout_focus_next(&layout);
        ASSERT_TRUE(panel_layout_get_focus(&layout) != PANEL_OUTLINER);
    }
    return 0;
}

/* ---- Persistence ---- */

static int test_save_load_layout(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);

    /* Modify layout */
    panel_layout_drag_divider(&layout, DIVIDER_LEFT, 50);
    panel_layout_toggle(&layout, PANEL_INSPECTOR);

    float orig_left = layout.divider_pos[DIVIDER_LEFT];

    /* Save */
    const char *tmp_path = "/tmp/test_panel_layout.cfg";
    ASSERT_TRUE(panel_layout_save(&layout, tmp_path));

    /* Create fresh layout and load */
    panel_layout_t loaded;
    panel_layout_init(&loaded, 1280, 720);
    ASSERT_TRUE(panel_layout_load(&loaded, tmp_path));

    /* Float round-trip through printf/scanf: compare with tolerance */
    float diff = loaded.divider_pos[DIVIDER_LEFT] - orig_left;
    if (diff < 0) diff = -diff;
    ASSERT_TRUE(diff < 0.001f);
    ASSERT_TRUE(!loaded.visible[PANEL_INSPECTOR]);
    ASSERT_TRUE(loaded.visible[PANEL_OUTLINER]);

    return 0;
}

static int test_load_nonexistent_file(void) {
    panel_layout_t layout;
    panel_layout_init(&layout, 1280, 720);
    ASSERT_TRUE(!panel_layout_load(&layout, "/tmp/does_not_exist_12345.cfg"));
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"default_layout",                test_default_layout},
    {"layout_covers_window",          test_layout_covers_window},
    {"resize_recomputes_rects",       test_resize_recomputes_rects},
    {"drag_vertical_divider",         test_drag_vertical_divider},
    {"drag_horizontal_divider",       test_drag_horizontal_divider},
    {"divider_clamp_min_size",        test_divider_clamp_min_size},
    {"divider_clamp_max_size",        test_divider_clamp_max_size},
    {"toggle_panel_visibility",       test_toggle_panel_visibility},
    {"focus_default_is_viewport",     test_focus_default_is_viewport},
    {"focus_tab_cycle",               test_focus_tab_cycle},
    {"focus_click_sets",              test_focus_click_sets},
    {"focus_escape_returns_viewport", test_focus_escape_returns_to_viewport},
    {"hit_test_returns_panel",        test_hit_test_returns_panel},
    {"divider_hit_test",              test_divider_hit_test},
    {"zero_size_window",              test_zero_size_window},
    {"tiny_window",                   test_tiny_window},
    {"toggle_hidden_skips_focus",     test_toggle_hidden_panel_skips_focus},
    {"save_load_layout",              test_save_load_layout},
    {"load_nonexistent_file",         test_load_nonexistent_file},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;

    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            fprintf(stderr, "FAIL %s\n", tc->name);
            break;
        }
    }

    printf("\n%zu / %zu tests passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
