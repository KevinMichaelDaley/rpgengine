/**
 * @file p104_engine_settings_tests.c
 * @brief Tests for the global engine settings (freeze-after-launch).
 */

#include <stdio.h>
#include <string.h>

/* Build with -DFR_NET_EMULATION to test emulation fields. */
#include "ferrum/engine_settings.h"

/* ── Test harness ───────────────────────────────────────────────── */

static int g_test_count = 0;
static int g_fail_count = 0;

#define RUN_TEST(fn) do { \
    g_test_count++; \
    printf("  %-50s ", #fn); \
    if ((fn)() == 0) { printf("PASS\n"); } \
    else { printf("FAIL\n"); g_fail_count++; } \
} while (0)

#define ASSERT_TRUE(cond) do { \
    if (!(cond)) { printf("ASSERT_TRUE(%s) failed at %s:%d\n", \
        #cond, __FILE__, __LINE__); return 1; } \
} while (0)

#define ASSERT_INT_EQ(expected, actual) do { \
    int _e = (expected), _a = (actual); \
    if (_e != _a) { printf("ASSERT_INT_EQ(%d, %d) failed at %s:%d\n", \
        _e, _a, __FILE__, __LINE__); return 1; } \
} while (0)

/* ── Tests ──────────────────────────────────────────────────────── */

static int test_get_before_init_returns_null(void) {
    /* Before init, get should return NULL. */
    ASSERT_TRUE(fr_engine_settings_get() == NULL);
    return 0;
}

static int test_init_and_mut(void) {
    ASSERT_INT_EQ(0, fr_engine_settings_init());
    fr_engine_settings_t *s = fr_engine_settings_mut();
    ASSERT_TRUE(s != NULL);
    return 0;
}

static int test_get_before_freeze_returns_null(void) {
    /* After init but before freeze, get returns NULL. */
    ASSERT_TRUE(fr_engine_settings_get() == NULL);
    ASSERT_TRUE(!fr_engine_settings_is_frozen());
    return 0;
}

static int test_freeze_and_get(void) {
    ASSERT_INT_EQ(0, fr_engine_settings_freeze());
    const fr_engine_settings_t *s = fr_engine_settings_get();
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(fr_engine_settings_is_frozen());
    return 0;
}

static int test_mut_after_freeze_returns_null(void) {
    /* After freeze, mut returns NULL. */
    ASSERT_TRUE(fr_engine_settings_mut() == NULL);
    return 0;
}

static int test_init_after_freeze_fails(void) {
    ASSERT_INT_EQ(-1, fr_engine_settings_init());
    return 0;
}

#ifdef FR_NET_EMULATION
static int test_net_emu_defaults_zero(void) {
    /* Re-init for a clean slate (need to un-freeze first).
     * Since we can't un-freeze in production, this test relies on
     * a fresh process.  We'll just check the previously frozen values. */
    const fr_engine_settings_t *s = fr_engine_settings_get();
    ASSERT_TRUE(s != NULL);
    ASSERT_TRUE(s->net_emu.delay_ms == 0.0f);
    ASSERT_TRUE(s->net_emu.jitter_ms == 0.0f);
    ASSERT_TRUE(s->net_emu.loss_pct == 0.0f);
    ASSERT_TRUE(s->net_emu_enabled == 0);
    return 0;
}
#endif

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("p104_engine_settings_tests:\n");

    /* These tests must run in order (init → mut → freeze → get). */
    RUN_TEST(test_get_before_init_returns_null);
    RUN_TEST(test_init_and_mut);
    RUN_TEST(test_get_before_freeze_returns_null);
    RUN_TEST(test_freeze_and_get);
    RUN_TEST(test_mut_after_freeze_returns_null);
    RUN_TEST(test_init_after_freeze_fails);
#ifdef FR_NET_EMULATION
    RUN_TEST(test_net_emu_defaults_zero);
#endif

    printf("\n%d/%d tests passed\n", g_test_count - g_fail_count, g_test_count);
    return g_fail_count ? 1 : 0;
}
