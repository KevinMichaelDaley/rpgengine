/**
 * @file p200f_autosave_tests.c
 * @brief Unit tests for edit autosave module.
 *
 * Tests autosave state management, interval timing, and force-save
 * triggering. Does NOT test actual file I/O. Headless.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/editor/protocol/edit_autosave.h"

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

/* ---- Tests ---- */

static int test_autosave_init(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 5000;
    cfg.save_path = "/tmp/test_level.json";

    bool ok = edit_autosave_init(&autosave, &cfg);
    ASSERT_TRUE(ok);
    ASSERT_INT_EQ(5000, (int)autosave.interval_ms);
    ASSERT_TRUE(!autosave.force_pending);
    ASSERT_TRUE(!autosave.dirty);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_destroy_safe(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);
    edit_autosave_destroy(&autosave);
    edit_autosave_destroy(&autosave);
    edit_autosave_destroy(NULL);
    return 0;
}

static int test_autosave_init_defaults(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.save_path = "/tmp/test.json";
    /* interval_ms = 0 → use default */

    bool ok = edit_autosave_init(&autosave, &cfg);
    ASSERT_TRUE(ok);
    ASSERT_TRUE(autosave.interval_ms > 0);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_init_null(void) {
    edit_autosave_t autosave;
    bool ok = edit_autosave_init(&autosave, NULL);
    ASSERT_TRUE(!ok);
    return 0;
}

static int test_autosave_mark_dirty(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);

    ASSERT_TRUE(!autosave.dirty);
    edit_autosave_mark_dirty(&autosave);
    ASSERT_TRUE(autosave.dirty);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_force_save(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);

    edit_autosave_request_force(&autosave);
    ASSERT_TRUE(autosave.force_pending);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_should_save_not_dirty(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);

    /* Not dirty, not forced, not expired → should not save */
    bool should = edit_autosave_should_save(&autosave, 500);
    ASSERT_TRUE(!should);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_should_save_forced(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 10000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);

    edit_autosave_request_force(&autosave);
    /* Force should trigger save even without dirty and before interval */
    bool should = edit_autosave_should_save(&autosave, 100);
    ASSERT_TRUE(should);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_should_save_interval(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);

    edit_autosave_mark_dirty(&autosave);

    /* Before interval → no save */
    bool should = edit_autosave_should_save(&autosave, 500);
    ASSERT_TRUE(!should);

    /* After interval → save */
    should = edit_autosave_should_save(&autosave, 1500);
    ASSERT_TRUE(should);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_did_save_resets_state(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/tmp/test.json";
    edit_autosave_init(&autosave, &cfg);

    edit_autosave_mark_dirty(&autosave);
    edit_autosave_request_force(&autosave);

    /* Simulate save completion */
    edit_autosave_did_save(&autosave, 2000);
    ASSERT_TRUE(!autosave.dirty);
    ASSERT_TRUE(!autosave.force_pending);
    ASSERT_INT_EQ(2000, (int)autosave.last_save_ms);

    edit_autosave_destroy(&autosave);
    return 0;
}

static int test_autosave_save_path(void) {
    edit_autosave_t autosave;
    edit_autosave_config_t cfg = {0};
    cfg.interval_ms = 1000;
    cfg.save_path = "/data/levels/world.json";
    edit_autosave_init(&autosave, &cfg);

    ASSERT_TRUE(strcmp(autosave.save_path, "/data/levels/world.json") == 0);

    edit_autosave_destroy(&autosave);
    return 0;
}

/* ---- Test runner ---- */

struct test_case {
    const char *name;
    int (*fn)(void);
};

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static struct test_case TESTS[] = {
    {"autosave_init",                 test_autosave_init},
    {"autosave_destroy_safe",         test_autosave_destroy_safe},
    {"autosave_init_defaults",        test_autosave_init_defaults},
    {"autosave_init_null",            test_autosave_init_null},
    {"autosave_mark_dirty",           test_autosave_mark_dirty},
    {"autosave_force_save",           test_autosave_force_save},
    {"autosave_should_save_not_dirty", test_autosave_should_save_not_dirty},
    {"autosave_should_save_forced",   test_autosave_should_save_forced},
    {"autosave_should_save_interval", test_autosave_should_save_interval},
    {"autosave_did_save_resets",      test_autosave_did_save_resets_state},
    {"autosave_save_path",            test_autosave_save_path},
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
