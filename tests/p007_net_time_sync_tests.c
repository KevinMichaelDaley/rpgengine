/**
 * @file p007_net_time_sync_tests.c
 * @brief RED tests for time synchronization and jitter buffer.
 *
 * Time sync model:
 *   - Server embeds its timestamp in each packet.
 *   - Client records arrival time and computes offset = server_time - client_time.
 *   - Median filter over N recent samples rejects jitter outliers.
 *   - Drift clamp limits offset change per update to avoid visual pops.
 *   - Jitter buffer adds a safety margin to the interpolation delay.
 *
 * Covers:
 *   1. Single sample sets initial offset
 *   2. Stable samples converge to correct offset
 *   3. Median filter rejects outlier
 *   4. Drift clamp limits offset change rate
 *   5. Jitter buffer computes interpolation time
 *   6. Jitter buffer adapts to changing jitter
 *   7. Edge cases: zero offset, negative offset
 *   8. NULL safety
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "ferrum/net/time_sync.h"

/* ── Minimal test harness ───────────────────────────────────────── */

static int g_pass_count;
static int g_fail_count;

#define TEST(name) static void name(void)
#define RUN(name) do { \
    printf("  %-52s ", #name); \
    name(); \
    printf("PASS\n"); \
    g_pass_count++; \
} while (0)

#define ASSERT(cond) do { \
    if (!(cond)) { \
        printf("FAIL\n    %s:%d: %s\n", __FILE__, __LINE__, #cond); \
        g_fail_count++; return; \
    } \
} while (0)

#define ASSERT_EQ(a, b) ASSERT((a) == (b))

/* ── Tests ──────────────────────────────────────────────────────── */

/**
 * First sample initializes the offset directly.
 */
TEST(test_initial_offset) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 8, 5); /* 8-sample window, 5ms/update drift clamp */

    /* Server clock is 1000ms ahead of client clock. */
    net_time_sync_sample(&sync, 5000, 4000); /* server=5000, client=4000 */

    /* Offset should be ~1000 (server - client). */
    int64_t offset = net_time_sync_offset(&sync);
    ASSERT_EQ(offset, 1000);
}

/**
 * Multiple stable samples converge to the correct offset.
 */
TEST(test_stable_convergence) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 8, 100);

    /* Server is 500ms ahead. Feed consistent samples. */
    for (int i = 0; i < 8; i++) {
        uint64_t client = 1000 + (uint64_t)(i * 50);
        uint64_t server = client + 500;
        net_time_sync_sample(&sync, server, client);
    }

    int64_t offset = net_time_sync_offset(&sync);
    ASSERT_EQ(offset, 500);
}

/**
 * Median filter rejects a single outlier among stable samples.
 */
TEST(test_median_rejects_outlier) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 7, 100);

    /* 6 stable samples at offset=500. */
    for (int i = 0; i < 6; i++) {
        uint64_t client = 1000 + (uint64_t)(i * 50);
        net_time_sync_sample(&sync, client + 500, client);
    }

    /* 1 outlier at offset=9999. */
    net_time_sync_sample(&sync, 1300 + 9999, 1300);

    /* Median of {500,500,500,500,500,500,9999} = 500. */
    int64_t offset = net_time_sync_offset(&sync);
    ASSERT_EQ(offset, 500);
}

/**
 * Drift clamp limits how fast the applied offset changes.
 * If median jumps by more than max_drift_per_update, the applied
 * offset moves by at most max_drift_per_update toward the median.
 */
TEST(test_drift_clamp) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 4, 3); /* very tight clamp: 3ms per update */

    /* Initial offset = 100. */
    for (int i = 0; i < 4; i++) {
        net_time_sync_sample(&sync, 1100, 1000);
    }
    ASSERT_EQ(net_time_sync_offset(&sync), 100);

    /* Suddenly offset jumps to 200 — all 4 samples. */
    for (int i = 0; i < 4; i++) {
        net_time_sync_sample(&sync, 1200, 1000);
    }

    /* With drift clamp=3, each of the 4 sample() calls moves the
     * applied offset by at most 3ms toward the new median.
     * Exact value depends on when median flips from 100→200.
     * Key invariant: offset moved toward 200 but didn't reach it. */
    int64_t offset = net_time_sync_offset(&sync);
    ASSERT(offset > 100);
    ASSERT(offset < 200);
}

/**
 * Jitter buffer computes an interpolation time that is behind
 * the estimated server time by the jitter margin.
 */
TEST(test_jitter_buffer_interpolation_time) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 4, 100);

    /* Offset = 500. */
    for (int i = 0; i < 4; i++) {
        net_time_sync_sample(&sync, 1500, 1000);
    }

    /* Feed jitter samples to establish the buffer margin. */
    net_jitter_buffer_t jbuf;
    net_jitter_buffer_init(&jbuf, 16);

    /* Simulate packets arriving with ~10ms jitter. */
    for (int i = 0; i < 8; i++) {
        uint64_t expected = 2000 + (uint64_t)(i * 50);
        /* Actual arrival jitters by ±5ms. */
        uint64_t actual = expected + (uint64_t)((i % 3) * 5);
        net_jitter_buffer_sample(&jbuf, expected, actual);
    }

    uint64_t margin = net_jitter_buffer_margin(&jbuf);
    /* Margin should be > 0 (at least the observed jitter). */
    ASSERT(margin > 0);

    /* Interpolation time = estimated_server_now - margin. */
    uint64_t client_now = 2500;
    int64_t offset = net_time_sync_offset(&sync);
    uint64_t est_server = client_now + (uint64_t)offset;
    uint64_t interp_time = est_server - margin;

    /* Interp time should be behind estimated server time. */
    ASSERT(interp_time < est_server);
}

/**
 * Jitter buffer adapts: higher jitter → larger margin.
 */
TEST(test_jitter_buffer_adapts) {
    net_jitter_buffer_t jbuf_low, jbuf_high;
    net_jitter_buffer_init(&jbuf_low, 8);
    net_jitter_buffer_init(&jbuf_high, 8);

    /* Low jitter: ±2ms. */
    for (int i = 0; i < 8; i++) {
        uint64_t base = 1000 + (uint64_t)(i * 50);
        net_jitter_buffer_sample(&jbuf_low, base, base + (uint64_t)(i % 2) * 2);
    }

    /* High jitter: ±20ms. */
    for (int i = 0; i < 8; i++) {
        uint64_t base = 1000 + (uint64_t)(i * 50);
        net_jitter_buffer_sample(&jbuf_high, base, base + (uint64_t)(i % 3) * 20);
    }

    uint64_t margin_low = net_jitter_buffer_margin(&jbuf_low);
    uint64_t margin_high = net_jitter_buffer_margin(&jbuf_high);

    ASSERT(margin_high > margin_low);
}

/**
 * Zero and negative offsets work correctly.
 */
TEST(test_zero_and_negative_offset) {
    net_time_sync_t sync;
    net_time_sync_init(&sync, 4, 100);

    /* Server behind client → negative offset. */
    for (int i = 0; i < 4; i++) {
        net_time_sync_sample(&sync, 900, 1000); /* offset = -100 */
    }
    ASSERT_EQ(net_time_sync_offset(&sync), -100);

    /* Exact sync → zero offset. */
    net_time_sync_t sync2;
    net_time_sync_init(&sync2, 4, 100);
    for (int i = 0; i < 4; i++) {
        net_time_sync_sample(&sync2, 1000, 1000);
    }
    ASSERT_EQ(net_time_sync_offset(&sync2), 0);
}

/**
 * NULL args are handled gracefully.
 */
TEST(test_null_safety) {
    net_time_sync_init(NULL, 4, 5); /* no-op */
    net_time_sync_sample(NULL, 100, 200); /* no-op */
    ASSERT_EQ(net_time_sync_offset(NULL), 0);

    net_jitter_buffer_init(NULL, 4); /* no-op */
    net_jitter_buffer_sample(NULL, 100, 200); /* no-op */
    ASSERT_EQ(net_jitter_buffer_margin(NULL), 0);
}

/* ── Runner ─────────────────────────────────────────────────────── */

int main(void) {
    printf("p007_net_time_sync_tests:\n");
    RUN(test_initial_offset);
    RUN(test_stable_convergence);
    RUN(test_median_rejects_outlier);
    RUN(test_drift_clamp);
    RUN(test_jitter_buffer_interpolation_time);
    RUN(test_jitter_buffer_adapts);
    RUN(test_zero_and_negative_offset);
    RUN(test_null_safety);
    printf("%d/%d tests passed\n", g_pass_count,
           g_pass_count + g_fail_count);
    return g_fail_count ? 1 : 0;
}
