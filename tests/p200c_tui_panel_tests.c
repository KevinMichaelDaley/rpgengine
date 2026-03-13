/**
 * @file p200c_tui_panel_tests.c
 * @brief Unit tests for the embedded TUI panel module.
 *
 * Tests the TUI panel's integration of ctrl_log_t and ctrl_tui_t
 * with Clay-based rendering. Headless — no OpenGL context required.
 */

#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "clay.h"

#include "ferrum/editor/ctrl_log.h"
#include "ferrum/editor/ctrl_tui.h"
#include "ferrum/editor/ui/tui_panel.h"

/* ---- Test harness ---- */

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

#define ASSERT_STR_EQ(exp, act)                                                \
    do {                                                                        \
        if (strcmp((exp), (act)) != 0) {                                        \
            fprintf(stderr, "ASSERT_STR_EQ failed: %s:%d: expected \"%s\" "   \
                    "got \"%s\"\n", __FILE__, __LINE__, (exp), (act));          \
            return 1;                                                          \
        }                                                                      \
    } while (0)

/* ---- Dummy text measurement callback ---- */

static Clay_Dimensions dummy_measure_text(Clay_StringSlice text,
                                           Clay_TextElementConfig *config,
                                           void *user_data) {
    (void)user_data;
    float w = (float)text.length * 8.0f;
    float h = (float)config->fontSize;
    return (Clay_Dimensions){w, h};
}

/* ---- Clay setup/teardown helpers ---- */

typedef struct test_clay_ctx {
    void *mem;
    Clay_Context *ctx;
} test_clay_ctx_t;

static bool setup_clay(test_clay_ctx_t *tc) {
    uint32_t size = Clay_MinMemorySize();
    tc->mem = malloc(size);
    if (!tc->mem) return false;
    Clay_Arena arena = Clay_CreateArenaWithCapacityAndMemory(size, tc->mem);
    Clay_Dimensions dims = {1280.0f, 720.0f};
    Clay_ErrorHandler err = {0};
    tc->ctx = Clay_Initialize(arena, dims, err);
    if (!tc->ctx) { free(tc->mem); return false; }
    Clay_SetMeasureTextFunction(dummy_measure_text, NULL);
    return true;
}

static void teardown_clay(test_clay_ctx_t *tc) {
    Clay_SetCurrentContext(NULL);
    free(tc->mem);
}

/* ---- Tests ---- */

/**
 * Test that tui_panel_init correctly initializes state.
 */
static int test_tui_panel_init(void) {
    tui_panel_t panel;
    tui_panel_config_t config = {0};
    config.log_capacity = 64;

    bool ok = tui_panel_init(&panel, &config);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(panel.tui.mode == CTRL_MODE_NORMAL);
    ASSERT_TRUE(panel.tui.log.capacity == 64);
    ASSERT_TRUE(panel.tui.cmd_len == 0);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test that tui_panel_destroy handles double-destroy and NULL.
 */
static int test_tui_panel_destroy_safe(void) {
    tui_panel_t panel;
    tui_panel_config_t config = {0};
    tui_panel_init(&panel, &config);
    tui_panel_destroy(&panel);
    /* Double destroy should not crash */
    tui_panel_destroy(&panel);
    /* NULL should not crash */
    tui_panel_destroy(NULL);
    return 0;
}

/**
 * Test that init with NULL config uses defaults.
 */
static int test_tui_panel_init_defaults(void) {
    tui_panel_t panel;
    bool ok = tui_panel_init(&panel, NULL);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(panel.tui.log.capacity == CTRL_LOG_DEFAULT_CAP);
    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test adding log entries and retrieving them.
 */
static int test_tui_panel_log_entries(void) {
    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    tui_panel_log(&panel, 0, "Hello world");
    tui_panel_log(&panel, 1, "Warning message");
    tui_panel_log(&panel, 2, "Error message");

    ASSERT_INT_EQ(3, (int)ctrl_log_visible_count(&panel.tui.log));

    const ctrl_log_entry_t *e0 = ctrl_log_get(&panel.tui.log, 0);
    ASSERT_TRUE(e0 != NULL);
    ASSERT_STR_EQ("Error message", e0->text);
    ASSERT_INT_EQ(2, (int)e0->level);

    const ctrl_log_entry_t *e2 = ctrl_log_get(&panel.tui.log, 2);
    ASSERT_TRUE(e2 != NULL);
    ASSERT_STR_EQ("Hello world", e2->text);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test command input via feed_key — enter command mode, type, submit.
 */
static int test_tui_panel_command_input(void) {
    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    /* Enter command mode with ':' */
    const char *result = tui_panel_feed_key(&panel, ':');
    ASSERT_TRUE(result == NULL);
    ASSERT_INT_EQ(CTRL_MODE_COMMAND, (int)panel.tui.mode);

    /* Type "spawn box" */
    const char *cmd = "spawn box";
    for (int i = 0; cmd[i]; i++) {
        result = tui_panel_feed_key(&panel, cmd[i]);
        ASSERT_TRUE(result == NULL);
    }
    ASSERT_INT_EQ(9, (int)panel.tui.cmd_len);

    /* Submit with Enter */
    result = tui_panel_feed_key(&panel, '\n');
    ASSERT_TRUE(result != NULL);
    ASSERT_STR_EQ("spawn box", result);
    ASSERT_INT_EQ(CTRL_MODE_NORMAL, (int)panel.tui.mode);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test command cancel via Escape.
 */
static int test_tui_panel_command_cancel(void) {
    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    tui_panel_feed_key(&panel, ':');
    tui_panel_feed_key(&panel, 'h');
    tui_panel_feed_key(&panel, 'i');
    ASSERT_INT_EQ(CTRL_MODE_COMMAND, (int)panel.tui.mode);

    /* Escape cancels */
    tui_panel_feed_key(&panel, 0x1B);
    ASSERT_INT_EQ(CTRL_MODE_NORMAL, (int)panel.tui.mode);
    ASSERT_INT_EQ(0, (int)panel.tui.cmd_len);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test scrollback with log entries.
 */
static int test_tui_panel_scrollback(void) {
    tui_panel_t panel;
    tui_panel_config_t config = {0};
    config.log_capacity = 16;
    tui_panel_init(&panel, &config);

    /* Fill with entries */
    for (int i = 0; i < 20; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Line %d", i);
        tui_panel_log(&panel, 0, buf);
    }

    /* Only last 16 should be available (ring wrapped) */
    ASSERT_INT_EQ(16, (int)ctrl_log_visible_count(&panel.tui.log));

    /* Bottom (newest) should be "Line 19" */
    const ctrl_log_entry_t *newest = ctrl_log_get(&panel.tui.log, 0);
    ASSERT_TRUE(newest != NULL);
    ASSERT_STR_EQ("Line 19", newest->text);

    /* Scroll up 5 lines */
    tui_panel_scroll(&panel, 5);
    ASSERT_INT_EQ(5, (int)panel.tui.log.scroll);

    /* Now bottom visible should be "Line 14" */
    const ctrl_log_entry_t *scrolled = ctrl_log_get(&panel.tui.log, 0);
    ASSERT_TRUE(scrolled != NULL);
    ASSERT_STR_EQ("Line 14", scrolled->text);

    /* Scroll back down */
    tui_panel_scroll(&panel, -5);
    ASSERT_INT_EQ(0, (int)panel.tui.log.scroll);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test that rendering via Clay produces text render commands.
 */
static int test_tui_panel_clay_render(void) {
    test_clay_ctx_t tc;
    ASSERT_TRUE(setup_clay(&tc));

    tui_panel_t panel;
    tui_panel_init(&panel, NULL);
    tui_panel_log(&panel, 0, "Test line 1");
    tui_panel_log(&panel, 1, "Warning line");

    /* Set up panel rect */
    tui_panel_rect_t rect = {0.0f, 500.0f, 1280.0f, 220.0f};

    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions((Clay_Dimensions){1280.0f, 720.0f});
    Clay_BeginLayout();

    tui_panel_render_clay(&panel, &rect);

    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Should have at least one text command for the log lines */
    int text_count = 0;
    for (int32_t i = 0; i < cmds.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            text_count++;
        }
    }
    ASSERT_TRUE(text_count >= 2); /* at least 2 log lines */

    tui_panel_destroy(&panel);
    teardown_clay(&tc);
    return 0;
}

/**
 * Test rendering empty log produces no text commands (except status bar).
 */
static int test_tui_panel_clay_render_empty(void) {
    test_clay_ctx_t tc;
    ASSERT_TRUE(setup_clay(&tc));

    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    tui_panel_rect_t rect = {0.0f, 500.0f, 1280.0f, 220.0f};

    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions((Clay_Dimensions){1280.0f, 720.0f});
    Clay_BeginLayout();

    tui_panel_render_clay(&panel, &rect);

    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Should still produce some commands (status bar, input line) */
    ASSERT_TRUE(cmds.length > 0);

    tui_panel_destroy(&panel);
    teardown_clay(&tc);
    return 0;
}

/**
 * Test command input is visible in rendered output.
 */
static int test_tui_panel_clay_render_input(void) {
    test_clay_ctx_t tc;
    ASSERT_TRUE(setup_clay(&tc));

    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    /* Enter command mode and type something */
    tui_panel_feed_key(&panel, ':');
    tui_panel_feed_key(&panel, 'h');
    tui_panel_feed_key(&panel, 'e');
    tui_panel_feed_key(&panel, 'l');
    tui_panel_feed_key(&panel, 'p');

    tui_panel_rect_t rect = {0.0f, 500.0f, 1280.0f, 220.0f};

    Clay_SetPointerState((Clay_Vector2){0, 0}, false);
    Clay_SetLayoutDimensions((Clay_Dimensions){1280.0f, 720.0f});
    Clay_BeginLayout();

    tui_panel_render_clay(&panel, &rect);

    Clay_RenderCommandArray cmds = Clay_EndLayout();

    /* Should have a text command containing the input line */
    bool found_input = false;
    for (int32_t i = 0; i < cmds.length; ++i) {
        Clay_RenderCommand *cmd = Clay_RenderCommandArray_Get(&cmds, i);
        if (cmd->commandType == CLAY_RENDER_COMMAND_TYPE_TEXT) {
            Clay_TextRenderData *td = &cmd->renderData.text;
            if (td->stringContents.length >= 5 &&
                memcmp(td->stringContents.chars, ":help", 5) == 0) {
                found_input = true;
                break;
            }
        }
    }
    ASSERT_TRUE(found_input);

    tui_panel_destroy(&panel);
    teardown_clay(&tc);
    return 0;
}

/**
 * Test status bar rendering includes mode indicator.
 */
static int test_tui_panel_status_bar(void) {
    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    /* Default status */
    char buf[256];
    tui_panel_format_status(&panel, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "NORMAL") != NULL);

    /* Switch to command mode */
    tui_panel_feed_key(&panel, ':');
    tui_panel_format_status(&panel, buf, sizeof(buf));
    ASSERT_TRUE(strstr(buf, "COMMAND") != NULL);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test ring buffer wrap-around preserves newest entries.
 */
static int test_tui_panel_log_wraparound(void) {
    tui_panel_t panel;
    tui_panel_config_t config = {0};
    config.log_capacity = 4;
    tui_panel_init(&panel, &config);

    tui_panel_log(&panel, 0, "A");
    tui_panel_log(&panel, 0, "B");
    tui_panel_log(&panel, 0, "C");
    tui_panel_log(&panel, 0, "D");
    tui_panel_log(&panel, 0, "E"); /* overwrites A */
    tui_panel_log(&panel, 0, "F"); /* overwrites B */

    ASSERT_INT_EQ(4, (int)ctrl_log_visible_count(&panel.tui.log));

    /* Newest = F, oldest available = C */
    const ctrl_log_entry_t *newest = ctrl_log_get(&panel.tui.log, 0);
    ASSERT_STR_EQ("F", newest->text);

    const ctrl_log_entry_t *oldest = ctrl_log_get(&panel.tui.log, 3);
    ASSERT_STR_EQ("C", oldest->text);

    tui_panel_destroy(&panel);
    return 0;
}

/**
 * Test command with pending/OK status tracking.
 */
static int test_tui_panel_cmd_status(void) {
    tui_panel_t panel;
    tui_panel_init(&panel, NULL);

    tui_panel_log_cmd(&panel, "spawn box", 42);
    const ctrl_log_entry_t *e = ctrl_log_get(&panel.tui.log, 0);
    ASSERT_TRUE(e != NULL);
    ASSERT_INT_EQ(CTRL_LOG_STATUS_PENDING, (int)e->status);
    ASSERT_INT_EQ(42, (int)e->cmd_id);

    /* Mark as OK */
    bool found = ctrl_log_set_cmd_status(&panel.tui.log, 42,
                                          CTRL_LOG_STATUS_OK);
    ASSERT_TRUE(found);

    e = ctrl_log_get(&panel.tui.log, 0);
    ASSERT_INT_EQ(CTRL_LOG_STATUS_OK, (int)e->status);

    tui_panel_destroy(&panel);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"tui_panel_init",            test_tui_panel_init},
    {"tui_panel_destroy_safe",    test_tui_panel_destroy_safe},
    {"tui_panel_init_defaults",   test_tui_panel_init_defaults},
    {"tui_panel_log_entries",     test_tui_panel_log_entries},
    {"tui_panel_command_input",   test_tui_panel_command_input},
    {"tui_panel_command_cancel",  test_tui_panel_command_cancel},
    {"tui_panel_scrollback",      test_tui_panel_scrollback},
    {"tui_panel_clay_render",     test_tui_panel_clay_render},
    {"tui_panel_clay_render_empty", test_tui_panel_clay_render_empty},
    {"tui_panel_clay_render_input", test_tui_panel_clay_render_input},
    {"tui_panel_status_bar",      test_tui_panel_status_bar},
    {"tui_panel_log_wraparound",  test_tui_panel_log_wraparound},
    {"tui_panel_cmd_status",      test_tui_panel_cmd_status},
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
