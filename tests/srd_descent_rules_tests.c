/**
 * @file srd_descent_rules_tests.c
 * @brief Tests for srd_descent_rules: table init, register, find, sample.
 */
#include "ferrum/procgen/srd/srd_descent_rules.h"

#include <stdio.h>
#include <string.h>

#define ASSERT_TRUE(cond)                                                    \
    do {                                                                     \
        if (!(cond)) {                                                       \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n",              \
                    __FILE__, __LINE__, #cond);                              \
            return 1;                                                        \
        }                                                                    \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                              \
    do {                                                                     \
        if ((exp) != (act)) {                                                \
            fprintf(stderr, "ASSERT_INT_EQ failed: %s:%d: expected %d "      \
                    "got %d\n", __FILE__, __LINE__, (int)(exp), (int)(act));  \
            return 1;                                                        \
        }                                                                    \
    } while (0)

/* ── Dummy rule cond/apply for testing ──────────────────────────── */

static bool dummy_cond_always_true(const srd_sdf_layout_t *layout,
                                   const srd_selection_t *sel,
                                   const void *userdata) {
    (void)layout; (void)sel; (void)userdata;
    return true;
}

static bool dummy_cond_always_false(const srd_sdf_layout_t *layout,
                                    const srd_selection_t *sel,
                                    const void *userdata) {
    (void)layout; (void)sel; (void)userdata;
    return false;
}

static int dummy_apply(srd_sdf_layout_t *layout,
                       const srd_selection_t *sel,
                       const void *userdata,
                       int *new_box_indices, int cap) {
    (void)layout; (void)sel; (void)userdata;
    (void)new_box_indices; (void)cap;
    return 0;
}

/* ── Helper: build a 2-box layout ───────────────────────────────── */

static void build_two_box_layout(srd_sdf_layout_t *layout) {
    srd_sdf_layout_init(layout);
    srd_sdf_box_t a = {1.0f, 1.0f, 1.0f, 1.0f, SRD_ROOM_GENERIC, 0, {0}};
    srd_sdf_box_t b = {4.0f, 1.0f, 1.0f, 1.0f, SRD_ROOM_BAR, 0, {0}};
    srd_sdf_layout_add_box(layout, &a);
    srd_sdf_layout_add_box(layout, &b);
    srd_sdf_layout_set_adj(layout, 0, 1, true);
}

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_table_init_empty(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    ASSERT_INT_EQ(0, tbl.n_rules);
    return 0;
}

static int test_register_returns_index(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t rule;
    memset(&rule, 0, sizeof(rule));
    rule.name = "test_rule";
    rule.inverse_rule_id = -1;
    rule.n_select = 1;
    rule.cond = dummy_cond_always_true;
    rule.apply = dummy_apply;

    int idx0 = srd_rule_table_register(&tbl, &rule);
    ASSERT_INT_EQ(0, idx0);

    rule.name = "test_rule_2";
    int idx1 = srd_rule_table_register(&tbl, &rule);
    ASSERT_INT_EQ(1, idx1);

    ASSERT_INT_EQ(2, tbl.n_rules);
    return 0;
}

static int test_register_null_fails(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);
    ASSERT_INT_EQ(-1, srd_rule_table_register(NULL, NULL));
    ASSERT_INT_EQ(-1, srd_rule_table_register(&tbl, NULL));
    return 0;
}

static int test_find_applicable_excludes_repair(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t normal = {0};
    normal.name = "normal";
    normal.inverse_rule_id = -1;
    normal.n_select = 1;
    normal.cond = dummy_cond_always_true;
    normal.apply = dummy_apply;
    srd_rule_table_register(&tbl, &normal);

    srd_descent_rule_t repair = {0};
    repair.name = "repair";
    repair.inverse_rule_id = -1;
    repair.n_select = 1;
    repair.is_repair = true;
    repair.cond = dummy_cond_always_true;
    repair.apply = dummy_apply;
    srd_rule_table_register(&tbl, &repair);

    srd_sdf_layout_t layout;
    build_two_box_layout(&layout);

    int out[16];
    uint32_t rng = 12345;
    int n = srd_rule_find_applicable(&tbl, &layout, out, 16, &rng);

    /* Only the normal rule should appear */
    ASSERT_INT_EQ(1, n);
    ASSERT_INT_EQ(0, out[0]);
    return 0;
}

static int test_find_applicable_cond_false_excluded(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t rule = {0};
    rule.name = "impossible";
    rule.inverse_rule_id = -1;
    rule.n_select = 1;
    rule.cond = dummy_cond_always_false;
    rule.apply = dummy_apply;
    srd_rule_table_register(&tbl, &rule);

    srd_sdf_layout_t layout;
    build_two_box_layout(&layout);

    int out[16];
    uint32_t rng = 12345;
    int n = srd_rule_find_applicable(&tbl, &layout, out, 16, &rng);
    ASSERT_INT_EQ(0, n);
    return 0;
}

static int test_sample_selection_valid(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t rule = {0};
    rule.name = "pick_one";
    rule.inverse_rule_id = -1;
    rule.n_select = 1;
    rule.cond = dummy_cond_always_true;
    rule.apply = dummy_apply;
    srd_rule_table_register(&tbl, &rule);

    srd_sdf_layout_t layout;
    build_two_box_layout(&layout);

    srd_selection_t sel;
    uint32_t rng = 54321;
    bool ok = srd_rule_sample_selection(&tbl, 0, &layout, &sel, &rng);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(1, sel.n);
    ASSERT_TRUE(sel.indices[0] >= 0 && sel.indices[0] < layout.n_boxes);
    return 0;
}

static int test_sample_selection_cond_false(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t rule = {0};
    rule.name = "impossible";
    rule.inverse_rule_id = -1;
    rule.n_select = 1;
    rule.cond = dummy_cond_always_false;
    rule.apply = dummy_apply;
    srd_rule_table_register(&tbl, &rule);

    srd_sdf_layout_t layout;
    build_two_box_layout(&layout);

    srd_selection_t sel;
    uint32_t rng = 54321;
    bool ok = srd_rule_sample_selection(&tbl, 0, &layout, &sel, &rng);
    ASSERT_TRUE(!ok);
    return 0;
}

static int test_sample_selection_two_boxes(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t rule = {0};
    rule.name = "pick_two";
    rule.inverse_rule_id = -1;
    rule.n_select = 2;
    rule.cond = dummy_cond_always_true;
    rule.apply = dummy_apply;
    srd_rule_table_register(&tbl, &rule);

    srd_sdf_layout_t layout;
    build_two_box_layout(&layout);

    srd_selection_t sel;
    uint32_t rng = 99999;
    bool ok = srd_rule_sample_selection(&tbl, 0, &layout, &sel, &rng);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(2, sel.n);
    /* Indices must be distinct */
    ASSERT_TRUE(sel.indices[0] != sel.indices[1]);
    return 0;
}

static int test_empty_layout_no_selection(void) {
    srd_rule_table_t tbl;
    srd_rule_table_init(&tbl);

    srd_descent_rule_t rule = {0};
    rule.name = "pick_one";
    rule.inverse_rule_id = -1;
    rule.n_select = 1;
    rule.cond = dummy_cond_always_true;
    rule.apply = dummy_apply;
    srd_rule_table_register(&tbl, &rule);

    srd_sdf_layout_t layout;
    srd_sdf_layout_init(&layout);

    srd_selection_t sel;
    uint32_t rng = 12345;
    bool ok = srd_rule_sample_selection(&tbl, 0, &layout, &sel, &rng);
    ASSERT_TRUE(!ok);
    return 0;
}

/* ── Test runner ────────────────────────────────────────────────── */

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"table_init_empty",              test_table_init_empty},
    {"register_returns_index",        test_register_returns_index},
    {"register_null_fails",           test_register_null_fails},
    {"find_applicable_excludes_repair", test_find_applicable_excludes_repair},
    {"find_applicable_cond_false",    test_find_applicable_cond_false_excluded},
    {"sample_selection_valid",        test_sample_selection_valid},
    {"sample_selection_cond_false",   test_sample_selection_cond_false},
    {"sample_selection_two_boxes",    test_sample_selection_two_boxes},
    {"empty_layout_no_selection",     test_empty_layout_no_selection},
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; i++) {
        struct test_case *tc = &TESTS[i];
        printf("RUN  %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("  OK %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL %s\n", tc->name);
        }
    }
    printf("\n%zu/%zu passed\n", passed, total);
    return (passed == total) ? 0 : 1;
}
