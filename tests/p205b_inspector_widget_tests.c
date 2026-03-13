/**
 * @file p205b_inspector_widget_tests.c
 * @brief Tests for inspector widget data model — float, vec3,
 *        dropdown, checkbox property editing.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/panels/inspector_widgets.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

#define ASSERT_FLOAT_NEAR(exp, act, tol)                                       \
    do {                                                                        \
        float _d = (float)(exp) - (float)(act);                                \
        if (_d < 0) _d = -_d;                                                 \
        if (_d > (tol)) {                                                     \
            fprintf(stderr, "ASSERT_FLOAT_NEAR failed: %s:%d: "              \
                    "expected %.6f got %.6f\n",                               \
                    __FILE__, __LINE__, (double)(exp), (double)(act));         \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Float widget tests ---- */

static int test_float_widget_init(void) {
    inspector_float_widget_t w;
    inspector_float_widget_init(&w, "Scale X", 1.0f, 0.0f, 100.0f);

    ASSERT_TRUE(strcmp(w.label, "Scale X") == 0);
    ASSERT_FLOAT_NEAR(1.0f, w.value, 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, w.min_value, 0.0001f);
    ASSERT_FLOAT_NEAR(100.0f, w.max_value, 0.0001f);

    return 0;
}

static int test_float_widget_set_value(void) {
    inspector_float_widget_t w;
    inspector_float_widget_init(&w, "X", 0.0f, -10.0f, 10.0f);

    inspector_float_widget_set(&w, 5.5f);
    ASSERT_FLOAT_NEAR(5.5f, w.value, 0.0001f);

    return 0;
}

static int test_float_widget_clamp(void) {
    inspector_float_widget_t w;
    inspector_float_widget_init(&w, "X", 0.0f, -10.0f, 10.0f);

    inspector_float_widget_set(&w, 100.0f);
    ASSERT_FLOAT_NEAR(10.0f, w.value, 0.0001f);

    inspector_float_widget_set(&w, -100.0f);
    ASSERT_FLOAT_NEAR(-10.0f, w.value, 0.0001f);

    return 0;
}

/* ---- Vec3 widget tests ---- */

static int test_vec3_widget_init(void) {
    inspector_vec3_widget_t w;
    float init[3] = {1.0f, 2.0f, 3.0f};
    inspector_vec3_widget_init(&w, "Position", init);

    ASSERT_TRUE(strcmp(w.label, "Position") == 0);
    ASSERT_FLOAT_NEAR(1.0f, w.value[0], 0.0001f);
    ASSERT_FLOAT_NEAR(2.0f, w.value[1], 0.0001f);
    ASSERT_FLOAT_NEAR(3.0f, w.value[2], 0.0001f);

    return 0;
}

static int test_vec3_widget_set_component(void) {
    inspector_vec3_widget_t w;
    float init[3] = {0.0f, 0.0f, 0.0f};
    inspector_vec3_widget_init(&w, "Rot", init);

    inspector_vec3_widget_set_component(&w, 1, 45.0f);
    ASSERT_FLOAT_NEAR(0.0f, w.value[0], 0.0001f);
    ASSERT_FLOAT_NEAR(45.0f, w.value[1], 0.0001f);
    ASSERT_FLOAT_NEAR(0.0f, w.value[2], 0.0001f);

    return 0;
}

static int test_vec3_widget_set_component_out_of_range(void) {
    inspector_vec3_widget_t w;
    float init[3] = {1.0f, 2.0f, 3.0f};
    inspector_vec3_widget_init(&w, "Scale", init);

    /* Invalid index should be no-op. */
    inspector_vec3_widget_set_component(&w, 5, 99.0f);
    ASSERT_FLOAT_NEAR(1.0f, w.value[0], 0.0001f);
    ASSERT_FLOAT_NEAR(2.0f, w.value[1], 0.0001f);
    ASSERT_FLOAT_NEAR(3.0f, w.value[2], 0.0001f);

    return 0;
}

/* ---- Dropdown widget tests ---- */

static int test_dropdown_widget_init(void) {
    const char *options[] = {"Box", "Sphere", "Capsule"};
    inspector_dropdown_widget_t w;
    inspector_dropdown_widget_init(&w, "Type", options, 3, 0);

    ASSERT_TRUE(strcmp(w.label, "Type") == 0);
    ASSERT_TRUE(w.selected_index == 0);
    ASSERT_TRUE(w.option_count == 3);
    ASSERT_TRUE(strcmp(w.options[0], "Box") == 0);

    return 0;
}

static int test_dropdown_widget_select(void) {
    const char *options[] = {"Box", "Sphere", "Capsule"};
    inspector_dropdown_widget_t w;
    inspector_dropdown_widget_init(&w, "Type", options, 3, 0);

    inspector_dropdown_widget_select(&w, 2);
    ASSERT_TRUE(w.selected_index == 2);

    /* Out of range — should clamp to last. */
    inspector_dropdown_widget_select(&w, 10);
    ASSERT_TRUE(w.selected_index == 2);

    return 0;
}

/* ---- Checkbox widget tests ---- */

static int test_checkbox_widget_init(void) {
    inspector_checkbox_widget_t w;
    inspector_checkbox_widget_init(&w, "Visible", true);

    ASSERT_TRUE(strcmp(w.label, "Visible") == 0);
    ASSERT_TRUE(w.checked == true);

    return 0;
}

static int test_checkbox_widget_toggle(void) {
    inspector_checkbox_widget_t w;
    inspector_checkbox_widget_init(&w, "Active", false);

    inspector_checkbox_widget_toggle(&w);
    ASSERT_TRUE(w.checked == true);

    inspector_checkbox_widget_toggle(&w);
    ASSERT_TRUE(w.checked == false);

    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"float_widget_init",            test_float_widget_init},
    {"float_widget_set_value",       test_float_widget_set_value},
    {"float_widget_clamp",           test_float_widget_clamp},
    {"vec3_widget_init",             test_vec3_widget_init},
    {"vec3_widget_set_component",    test_vec3_widget_set_component},
    {"vec3_widget_set_oob",          test_vec3_widget_set_component_out_of_range},
    {"dropdown_widget_init",         test_dropdown_widget_init},
    {"dropdown_widget_select",       test_dropdown_widget_select},
    {"checkbox_widget_init",         test_checkbox_widget_init},
    {"checkbox_widget_toggle",       test_checkbox_widget_toggle},
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
