/**
 * @file p205d_toolbar_tests.c
 * @brief Tests for editor toolbar — mode buttons, snap toggles,
 *        button state management.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/editor/panels/panel_toolbar.h"

/* ---- Test harness ---- */

#define ASSERT_TRUE(cond)                                                      \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__,      \
                    __LINE__, #cond);                                          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Tests ---- */

static int test_toolbar_init(void) {
    editor_toolbar_t tb;
    editor_toolbar_init(&tb);

    ASSERT_TRUE(tb.active_transform == TOOLBAR_TRANSFORM_TRANSLATE);
    ASSERT_TRUE(!tb.snap_enabled);
    ASSERT_TRUE(tb.button_count > 0);

    editor_toolbar_destroy(&tb);
    return 0;
}

static int test_toolbar_set_transform(void) {
    editor_toolbar_t tb;
    editor_toolbar_init(&tb);

    editor_toolbar_set_transform(&tb, TOOLBAR_TRANSFORM_ROTATE);
    ASSERT_TRUE(tb.active_transform == TOOLBAR_TRANSFORM_ROTATE);

    editor_toolbar_set_transform(&tb, TOOLBAR_TRANSFORM_SCALE);
    ASSERT_TRUE(tb.active_transform == TOOLBAR_TRANSFORM_SCALE);

    editor_toolbar_destroy(&tb);
    return 0;
}

static int test_toolbar_toggle_snap(void) {
    editor_toolbar_t tb;
    editor_toolbar_init(&tb);

    ASSERT_TRUE(!tb.snap_enabled);
    editor_toolbar_toggle_snap(&tb);
    ASSERT_TRUE(tb.snap_enabled);
    editor_toolbar_toggle_snap(&tb);
    ASSERT_TRUE(!tb.snap_enabled);

    editor_toolbar_destroy(&tb);
    return 0;
}

static int test_toolbar_get_button(void) {
    editor_toolbar_t tb;
    editor_toolbar_init(&tb);

    const toolbar_button_t *btn = editor_toolbar_get_button(&tb, 0);
    ASSERT_TRUE(btn != NULL);
    ASSERT_TRUE(btn->label[0] != '\0');

    /* Out of bounds. */
    const toolbar_button_t *oob = editor_toolbar_get_button(&tb,
                                                             tb.button_count + 5);
    ASSERT_TRUE(oob == NULL);

    editor_toolbar_destroy(&tb);
    return 0;
}

static int test_toolbar_button_active_state(void) {
    editor_toolbar_t tb;
    editor_toolbar_init(&tb);

    /* The translate button should be active by default. */
    bool found_active = false;
    for (uint32_t i = 0; i < tb.button_count; i++) {
        const toolbar_button_t *btn = editor_toolbar_get_button(&tb, i);
        if (btn->id == TOOLBAR_BTN_TRANSLATE) {
            found_active = true;
            ASSERT_TRUE(btn->active);
        }
    }
    ASSERT_TRUE(found_active);

    editor_toolbar_destroy(&tb);
    return 0;
}

static int test_toolbar_switch_updates_active(void) {
    editor_toolbar_t tb;
    editor_toolbar_init(&tb);

    editor_toolbar_set_transform(&tb, TOOLBAR_TRANSFORM_ROTATE);

    /* Translate button should no longer be active. */
    for (uint32_t i = 0; i < tb.button_count; i++) {
        const toolbar_button_t *btn = editor_toolbar_get_button(&tb, i);
        if (btn->id == TOOLBAR_BTN_TRANSLATE) {
            ASSERT_TRUE(!btn->active);
        }
        if (btn->id == TOOLBAR_BTN_ROTATE) {
            ASSERT_TRUE(btn->active);
        }
    }

    editor_toolbar_destroy(&tb);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"toolbar_init",                 test_toolbar_init},
    {"toolbar_set_transform",        test_toolbar_set_transform},
    {"toolbar_toggle_snap",          test_toolbar_toggle_snap},
    {"toolbar_get_button",           test_toolbar_get_button},
    {"toolbar_button_active_state",  test_toolbar_button_active_state},
    {"toolbar_switch_updates_active", test_toolbar_switch_updates_active},
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
