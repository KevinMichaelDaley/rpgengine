/**
 * @file ctrl_tui_tests.c
 * @brief Tests for controller TUI: log buffer, input state machine, rendering.
 */

#include <stdio.h>
#include <string.h>

#include "ferrum/editor/ctrl_tui.h"
#include "ferrum/editor/ctrl_log.h"

/* ----------------------------------------------------------------------- */
/* Test macros                                                               */
/* ----------------------------------------------------------------------- */

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

/* ----------------------------------------------------------------------- */
/* Log buffer tests                                                          */
/* ----------------------------------------------------------------------- */

/** Init and destroy log. */
static bool test_log_init_destroy(void) {
    ctrl_log_t log;
    ASSERT(ctrl_log_init(&log, 16));
    ASSERT(ctrl_log_visible_count(&log) == 0);
    ctrl_log_destroy(&log);
    return true;
}

/** Add entries and retrieve. */
static bool test_log_add(void) {
    ctrl_log_t log;
    ctrl_log_init(&log, 16);

    ctrl_log_add(&log, 0, "Hello world");
    ASSERT(ctrl_log_visible_count(&log) == 1);

    const ctrl_log_entry_t *e = ctrl_log_get(&log, 0);
    ASSERT(e != NULL);
    ASSERT(strcmp(e->text, "Hello world") == 0);
    ASSERT(e->level == 0);

    ctrl_log_destroy(&log);
    return true;
}

/** Log ring wraps when full — oldest entries overwritten. */
static bool test_log_ring_wrap(void) {
    ctrl_log_t log;
    ctrl_log_init(&log, 4);

    ctrl_log_add(&log, 0, "A");
    ctrl_log_add(&log, 0, "B");
    ctrl_log_add(&log, 0, "C");
    ctrl_log_add(&log, 0, "D");
    ctrl_log_add(&log, 0, "E"); /* Overwrites A. */

    ASSERT(ctrl_log_visible_count(&log) == 4);

    /* Newest (index 0) should be "E". */
    const ctrl_log_entry_t *newest = ctrl_log_get(&log, 0);
    ASSERT(newest && strcmp(newest->text, "E") == 0);

    /* Oldest available (index 3) should be "B". */
    const ctrl_log_entry_t *oldest = ctrl_log_get(&log, 3);
    ASSERT(oldest && strcmp(oldest->text, "B") == 0);

    ctrl_log_destroy(&log);
    return true;
}

/** Scroll up into history, then back down. */
static bool test_log_scroll(void) {
    ctrl_log_t log;
    ctrl_log_init(&log, 16);

    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "Line %d", i);
        ctrl_log_add(&log, 0, buf);
    }

    /* Initially scroll=0, index 0 = newest = "Line 9". */
    const ctrl_log_entry_t *e = ctrl_log_get(&log, 0);
    ASSERT(e && strcmp(e->text, "Line 9") == 0);

    /* Scroll up 3 lines. Now index 0 = "Line 6". */
    ctrl_log_scroll_up(&log, 3);
    ASSERT(log.scroll == 3);
    e = ctrl_log_get(&log, 0);
    ASSERT(e && strcmp(e->text, "Line 6") == 0);

    /* Scroll down 2 lines. Now index 0 = "Line 8". */
    ctrl_log_scroll_down(&log, 2);
    ASSERT(log.scroll == 1);
    e = ctrl_log_get(&log, 0);
    ASSERT(e && strcmp(e->text, "Line 8") == 0);

    ctrl_log_destroy(&log);
    return true;
}

/** Scroll clamps to available entries. */
static bool test_log_scroll_clamp(void) {
    ctrl_log_t log;
    ctrl_log_init(&log, 16);

    ctrl_log_add(&log, 0, "Only entry");

    /* Can't scroll up more than available. */
    ctrl_log_scroll_up(&log, 100);
    ASSERT(log.scroll == 0); /* Only 1 entry, can't scroll past it. */

    /* Scroll down past zero stays at zero. */
    ctrl_log_scroll_down(&log, 100);
    ASSERT(log.scroll == 0);

    ctrl_log_destroy(&log);
    return true;
}

/** Log clear resets everything. */
static bool test_log_clear(void) {
    ctrl_log_t log;
    ctrl_log_init(&log, 16);

    ctrl_log_add(&log, 0, "Text");
    ASSERT(ctrl_log_visible_count(&log) == 1);

    ctrl_log_clear(&log);
    ASSERT(ctrl_log_visible_count(&log) == 0);

    ctrl_log_destroy(&log);
    return true;
}

/* ----------------------------------------------------------------------- */
/* TUI lifecycle tests                                                       */
/* ----------------------------------------------------------------------- */

/** Init and destroy TUI. */
static bool test_tui_init_destroy(void) {
    ctrl_tui_t tui;
    ASSERT(ctrl_tui_init(&tui));
    ASSERT(tui.mode == CTRL_MODE_NORMAL);
    ASSERT(tui.screen_buf != NULL);
    ctrl_tui_destroy(&tui);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Input state machine tests                                                 */
/* ----------------------------------------------------------------------- */

/** ':' in Normal mode → Command mode. */
static bool test_input_colon_to_command(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, ':');
    ASSERT(tui.mode == CTRL_MODE_COMMAND);
    ASSERT(tui.cmd_len == 0); /* Colon itself not in buffer. */

    ctrl_tui_destroy(&tui);
    return true;
}

/** Escape in Command mode → back to Normal. */
static bool test_input_escape_to_normal(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, ':');
    ASSERT(tui.mode == CTRL_MODE_COMMAND);

    ctrl_tui_feed_key(&tui, 0x1B); /* ESC */
    ASSERT(tui.mode == CTRL_MODE_NORMAL);
    ASSERT(tui.cmd_len == 0); /* Command cleared. */

    ctrl_tui_destroy(&tui);
    return true;
}

/** Typing in Command mode builds command text. */
static bool test_input_command_typing(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, ':');
    ctrl_tui_feed_key(&tui, 's');
    ctrl_tui_feed_key(&tui, 'p');
    ctrl_tui_feed_key(&tui, 'a');
    ctrl_tui_feed_key(&tui, 'w');
    ctrl_tui_feed_key(&tui, 'n');

    ASSERT(tui.cmd_len == 5);
    ASSERT(memcmp(tui.cmd_text, "spawn", 5) == 0);

    ctrl_tui_destroy(&tui);
    return true;
}

/** Enter in Command mode returns the command string and resets. */
static bool test_input_command_enter(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, ':');
    ctrl_tui_feed_key(&tui, 'h');
    ctrl_tui_feed_key(&tui, 'i');

    const char *cmd = ctrl_tui_feed_key(&tui, '\r');
    ASSERT(cmd != NULL);
    ASSERT(strcmp(cmd, "hi") == 0);
    ASSERT(tui.mode == CTRL_MODE_NORMAL);
    ASSERT(tui.cmd_len == 0);

    ctrl_tui_destroy(&tui);
    return true;
}

/** Backspace in Command mode deletes last character. */
static bool test_input_command_backspace(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, ':');
    ctrl_tui_feed_key(&tui, 'a');
    ctrl_tui_feed_key(&tui, 'b');
    ctrl_tui_feed_key(&tui, 'c');
    ctrl_tui_feed_key(&tui, 0x7F); /* DEL/Backspace */

    ASSERT(tui.cmd_len == 2);
    ASSERT(memcmp(tui.cmd_text, "ab", 2) == 0);

    ctrl_tui_destroy(&tui);
    return true;
}

/** Backspace on empty command line in Command mode → back to Normal. */
static bool test_input_backspace_empty_exits(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, ':');
    ASSERT(tui.mode == CTRL_MODE_COMMAND);

    ctrl_tui_feed_key(&tui, 0x7F); /* Backspace on empty → exit. */
    ASSERT(tui.mode == CTRL_MODE_NORMAL);

    ctrl_tui_destroy(&tui);
    return true;
}

/** Numeric prefix accumulation in Normal mode. */
static bool test_input_numeric_prefix(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);

    ctrl_tui_feed_key(&tui, '3');
    ctrl_tui_feed_key(&tui, '5');
    ASSERT(tui.numeric_prefix == 35);
    ASSERT(tui.mode == CTRL_MODE_NORMAL);

    /* Non-digit resets prefix after use. */
    ctrl_tui_feed_key(&tui, ':');
    ASSERT(tui.mode == CTRL_MODE_COMMAND);
    ASSERT(tui.numeric_prefix == 0);

    ctrl_tui_destroy(&tui);
    return true;
}

/** Render produces non-empty screen buffer. */
static bool test_render_basic(void) {
    ctrl_tui_t tui;
    ctrl_tui_init(&tui);
    tui.cols = 80;
    tui.rows = 24;

    ctrl_log_add(&tui.log, 0, "Test message");
    ctrl_tui_render(&tui);

    ASSERT(tui.screen_len > 0);
    /* Should contain ANSI escape sequences. */
    ASSERT(memchr(tui.screen_buf, '\033', tui.screen_len) != NULL);

    ctrl_tui_destroy(&tui);
    return true;
}

/** Null params should not crash. */
static bool test_null_params(void) {
    ASSERT(!ctrl_tui_init(NULL));
    ctrl_tui_destroy(NULL);
    ASSERT(ctrl_tui_feed_key(NULL, 'a') == NULL);

    ASSERT(!ctrl_log_init(NULL, 0));
    ctrl_log_destroy(NULL);
    ctrl_log_add(NULL, 0, NULL);
    ASSERT(ctrl_log_get(NULL, 0) == NULL);
    return true;
}

/* ----------------------------------------------------------------------- */
/* Main                                                                      */
/* ----------------------------------------------------------------------- */

int main(void) {
    /* Log tests. */
    RUN(test_log_init_destroy);
    RUN(test_log_add);
    RUN(test_log_ring_wrap);
    RUN(test_log_scroll);
    RUN(test_log_scroll_clamp);
    RUN(test_log_clear);

    /* TUI tests. */
    RUN(test_tui_init_destroy);
    RUN(test_input_colon_to_command);
    RUN(test_input_escape_to_normal);
    RUN(test_input_command_typing);
    RUN(test_input_command_enter);
    RUN(test_input_command_backspace);
    RUN(test_input_backspace_empty_exits);
    RUN(test_input_numeric_prefix);
    RUN(test_render_basic);
    RUN(test_null_params);

    printf("\n%d / %d tests passed\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
