/**
 * @file p103_net_emulator_tests.c
 * @brief Tests for the in-process network condition emulator.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>

#include "ferrum/net/emulation/net_emulator.h"

/* ── Test harness macros ────────────────────────────────────────── */

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

/* ── Helpers ────────────────────────────────────────────────────── */

static net_udp_addr_t make_addr(uint8_t tag) {
    net_udp_addr_t a;
    memset(&a, 0, sizeof(a));
    a.storage[0] = tag;
    a.len = 1;
    return a;
}

/* ── Tests: lifecycle ───────────────────────────────────────────── */

static int test_default_config(void) {
    net_emu_config_t cfg = net_emu_config_default();
    ASSERT_TRUE(cfg.delay_ms == 0.0f);
    ASSERT_TRUE(cfg.jitter_ms == 0.0f);
    ASSERT_TRUE(cfg.loss_pct == 0.0f);
    ASSERT_TRUE(cfg.reorder_pct == 0.0f);
    ASSERT_TRUE(cfg.duplicate_pct == 0.0f);
    ASSERT_INT_EQ(NET_EMU_DIST_UNIFORM, (int)cfg.distribution);
    return 0;
}

static int test_init_destroy(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    ASSERT_INT_EQ(NET_EMU_OK, net_emulator_init(&emu, &cfg, 42));
    ASSERT_TRUE(net_emulator_is_enabled(&emu));
    ASSERT_INT_EQ(0, (int)net_emulator_pending(&emu));
    net_emulator_destroy(&emu);
    return 0;
}

static int test_init_null_safe(void) {
    net_emu_config_t cfg = net_emu_config_default();
    ASSERT_INT_EQ(NET_EMU_ERR_INVALID, net_emulator_init(NULL, &cfg, 0));

    net_emulator_t emu;
    ASSERT_INT_EQ(NET_EMU_ERR_INVALID, net_emulator_init(&emu, NULL, 0));

    /* destroy NULL should not crash. */
    net_emulator_destroy(NULL);
    return 0;
}

/* ── Tests: pass-through (zero delay) ───────────────────────────── */

static int test_zero_delay_passthrough(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    net_emulator_init(&emu, &cfg, 1);

    uint8_t payload[] = {0xDE, 0xAD, 0xBE, 0xEF};
    net_udp_addr_t addr = make_addr(1);
    uint64_t now = 1000000; /* 1 second */

    ASSERT_INT_EQ(NET_EMU_OK,
        net_emulator_submit(&emu, &addr, payload, sizeof(payload), now));
    ASSERT_INT_EQ(1, (int)net_emulator_pending(&emu));

    /* With zero delay, packet should be immediately available. */
    uint8_t out[64];
    size_t out_size = 0;
    net_udp_addr_t out_addr;
    ASSERT_INT_EQ(NET_EMU_OK,
        net_emulator_pop(&emu, &out_addr, out, sizeof(out), &out_size, now));

    ASSERT_INT_EQ(4, (int)out_size);
    ASSERT_TRUE(memcmp(out, payload, 4) == 0);
    ASSERT_TRUE(out_addr.storage[0] == 1);
    ASSERT_INT_EQ(0, (int)net_emulator_pending(&emu));

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: fixed delay ─────────────────────────────────────────── */

static int test_fixed_delay(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 50.0f; /* 50ms = 50000µs */
    net_emulator_init(&emu, &cfg, 1);

    uint8_t payload[] = {0x01};
    net_udp_addr_t addr = make_addr(2);
    uint64_t submit_time = 1000000;

    net_emulator_submit(&emu, &addr, payload, 1, submit_time);

    /* Not ready yet at submit time. */
    uint8_t out[64];
    size_t out_size = 0;
    net_udp_addr_t out_addr;
    int rc = net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                              &out_size, submit_time);
    ASSERT_TRUE(rc != NET_EMU_OK);
    ASSERT_INT_EQ(1, (int)net_emulator_pending(&emu));

    /* Not ready 10ms later. */
    rc = net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                          &out_size, submit_time + 10000);
    ASSERT_TRUE(rc != NET_EMU_OK);

    /* Ready at submit + 50ms. */
    rc = net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                          &out_size, submit_time + 50000);
    ASSERT_INT_EQ(NET_EMU_OK, rc);
    ASSERT_INT_EQ(1, (int)out_size);
    ASSERT_TRUE(out[0] == 0x01);

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: packet loss ─────────────────────────────────────────── */

static int test_100pct_loss(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.loss_pct = 100.0f;
    net_emulator_init(&emu, &cfg, 42);

    uint8_t payload[] = {0xFF};
    net_udp_addr_t addr = make_addr(3);

    /* Submit many packets — all should be dropped. */
    for (int i = 0; i < 50; i++) {
        net_emulator_submit(&emu, &addr, payload, 1, (uint64_t)i * 1000);
    }
    ASSERT_INT_EQ(0, (int)net_emulator_pending(&emu));

    net_emulator_destroy(&emu);
    return 0;
}

static int test_partial_loss(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.loss_pct = 50.0f;
    net_emulator_init(&emu, &cfg, 123);

    uint8_t payload[] = {0xAA};
    net_udp_addr_t addr = make_addr(4);

    int submitted = 1000;
    for (int i = 0; i < submitted; i++) {
        net_emulator_submit(&emu, &addr, payload, 1, (uint64_t)i * 100);
    }

    /* With 50% loss over 1000 packets, expect roughly 400-600 queued. */
    uint32_t pending = net_emulator_pending(&emu);
    ASSERT_TRUE(pending > 300);
    ASSERT_TRUE(pending < 700);

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: jitter distribution ─────────────────────────────────── */

static int test_uniform_jitter_range(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 40.0f;
    cfg.jitter_ms = 10.0f;
    cfg.distribution = NET_EMU_DIST_UNIFORM;
    net_emulator_init(&emu, &cfg, 99);

    uint8_t payload[] = {0x01};
    net_udp_addr_t addr = make_addr(5);
    uint64_t now = 0;

    /* Submit many packets and check release times are within
     * [delay - jitter, delay + jitter] = [30ms, 50ms]. */
    int count = 200;
    for (int i = 0; i < count; i++) {
        net_emulator_submit(&emu, &addr, payload, 1, now);
    }

    /* Pop all at a far future time, verify they all arrive. */
    uint8_t out[64];
    size_t out_size;
    net_udp_addr_t out_addr;
    int popped = 0;

    /* At 29ms none should be ready if min delay is 30ms. */
    int early_rc = net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                                    &out_size, 29000);
    /* Some might be ready due to uniform being [30,50] — but at 29ms, none. */
    /* Actually with uniform [30000,50000] µs, nothing at 29000. */
    ASSERT_TRUE(early_rc != NET_EMU_OK);

    /* At 50ms+ all should be available. */
    uint64_t far = 100000; /* 100ms */
    while (net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                            &out_size, far) == NET_EMU_OK) {
        popped++;
    }
    ASSERT_INT_EQ(count, popped);

    net_emulator_destroy(&emu);
    return 0;
}

static int test_normal_distribution(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 40.0f;
    cfg.jitter_ms = 5.0f;
    cfg.distribution = NET_EMU_DIST_NORMAL;
    net_emulator_init(&emu, &cfg, 77);

    uint8_t payload[] = {0x02};
    net_udp_addr_t addr = make_addr(6);

    int count = 200;
    for (int i = 0; i < count; i++) {
        net_emulator_submit(&emu, &addr, payload, 1, 0);
    }

    /* All should arrive eventually. */
    uint8_t out[64];
    size_t out_size;
    net_udp_addr_t out_addr;
    int popped = 0;
    uint64_t far = 500000; /* 500ms — way past any reasonable delay */
    while (net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                            &out_size, far) == NET_EMU_OK) {
        popped++;
    }
    ASSERT_INT_EQ(count, popped);

    net_emulator_destroy(&emu);
    return 0;
}

static int test_lognormal_distribution(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 40.0f;
    cfg.jitter_ms = 10.0f;
    cfg.distribution = NET_EMU_DIST_LOG_NORMAL;
    net_emulator_init(&emu, &cfg, 55);

    uint8_t payload[] = {0x03};
    net_udp_addr_t addr = make_addr(7);

    int count = 200;
    for (int i = 0; i < count; i++) {
        net_emulator_submit(&emu, &addr, payload, 1, 0);
    }

    /* All should arrive eventually (log-normal has long tail but finite). */
    uint8_t out[64];
    size_t out_size;
    net_udp_addr_t out_addr;
    int popped = 0;
    uint64_t far = 2000000; /* 2 seconds */
    while (net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                            &out_size, far) == NET_EMU_OK) {
        popped++;
    }
    ASSERT_INT_EQ(count, popped);

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: reorder ─────────────────────────────────────────────── */

static int test_reorder(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 10.0f;
    cfg.reorder_pct = 100.0f; /* force all reordered */
    net_emulator_init(&emu, &cfg, 33);

    net_udp_addr_t addr = make_addr(8);

    /* Submit packets with sequential IDs as payload. */
    for (uint8_t i = 0; i < 50; i++) {
        net_emulator_submit(&emu, &addr, &i, 1, (uint64_t)i * 1000);
    }

    /* Pop all and check that at least some are out of order. */
    uint8_t out[64];
    size_t out_size;
    net_udp_addr_t out_addr;
    uint64_t far = 1000000;
    int out_of_order = 0;
    uint8_t last = 0;
    int first = 1;
    int popped = 0;

    while (net_emulator_pop(&emu, &out_addr, out, sizeof(out),
                            &out_size, far) == NET_EMU_OK) {
        if (!first && out[0] < last) {
            out_of_order++;
        }
        last = out[0];
        first = 0;
        popped++;
    }

    ASSERT_INT_EQ(50, popped);
    /* With 100% reorder, expect significant out-of-order delivery. */
    ASSERT_TRUE(out_of_order > 5);

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: enable/disable ──────────────────────────────────────── */

static int test_enable_disable(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 100.0f;
    net_emulator_init(&emu, &cfg, 1);

    ASSERT_TRUE(net_emulator_is_enabled(&emu));

    net_emulator_set_enabled(&emu, 0);
    ASSERT_TRUE(!net_emulator_is_enabled(&emu));

    net_emulator_set_enabled(&emu, 1);
    ASSERT_TRUE(net_emulator_is_enabled(&emu));

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: runtime reconfigure ─────────────────────────────────── */

static int test_reconfigure(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 100.0f;
    net_emulator_init(&emu, &cfg, 1);

    /* Submit a packet with 100ms delay. */
    uint8_t p1[] = {0x01};
    net_udp_addr_t addr = make_addr(9);
    net_emulator_submit(&emu, &addr, p1, 1, 0);

    /* Reconfigure to 0ms delay. */
    cfg.delay_ms = 0.0f;
    net_emulator_configure(&emu, &cfg);

    /* Submit a second packet — should be immediately available. */
    uint8_t p2[] = {0x02};
    net_emulator_submit(&emu, &addr, p2, 1, 0);

    /* Pop: first available should be p2 (0ms delay), not p1 (100ms). */
    uint8_t out[64];
    size_t out_size;
    net_udp_addr_t out_addr;
    ASSERT_INT_EQ(NET_EMU_OK,
        net_emulator_pop(&emu, &out_addr, out, sizeof(out), &out_size, 0));
    ASSERT_TRUE(out[0] == 0x02);

    /* p1 still pending (100ms not elapsed). */
    ASSERT_INT_EQ(1, (int)net_emulator_pending(&emu));

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: queue full ──────────────────────────────────────────── */

static int test_queue_full(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.delay_ms = 1000.0f; /* large delay so nothing pops */
    net_emulator_init(&emu, &cfg, 1);

    uint8_t payload[] = {0xFF};
    net_udp_addr_t addr = make_addr(10);

    /* Fill queue to capacity. */
    for (uint32_t i = 0; i < NET_EMU_QUEUE_CAPACITY; i++) {
        ASSERT_INT_EQ(NET_EMU_OK,
            net_emulator_submit(&emu, &addr, payload, 1, 0));
    }
    ASSERT_INT_EQ(NET_EMU_QUEUE_CAPACITY, net_emulator_pending(&emu));

    /* Next submit should fail. */
    ASSERT_INT_EQ(NET_EMU_ERR_FULL,
        net_emulator_submit(&emu, &addr, payload, 1, 0));

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: duplicate packets ───────────────────────────────────── */

static int test_duplicate(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    cfg.duplicate_pct = 100.0f; /* duplicate everything */
    net_emulator_init(&emu, &cfg, 42);

    uint8_t payload[] = {0xBB};
    net_udp_addr_t addr = make_addr(11);

    /* Submit 10 packets — each should be duplicated → 20 in queue. */
    for (int i = 0; i < 10; i++) {
        net_emulator_submit(&emu, &addr, payload, 1, 0);
    }
    ASSERT_INT_EQ(20, (int)net_emulator_pending(&emu));

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Tests: oversized packet rejected ───────────────────────────── */

static int test_oversized_rejected(void) {
    net_emulator_t emu;
    net_emu_config_t cfg = net_emu_config_default();
    net_emulator_init(&emu, &cfg, 1);

    uint8_t big[NET_EMU_MAX_PACKET_SIZE + 1];
    memset(big, 0, sizeof(big));
    net_udp_addr_t addr = make_addr(12);

    ASSERT_INT_EQ(NET_EMU_ERR_INVALID,
        net_emulator_submit(&emu, &addr, big, sizeof(big), 0));
    ASSERT_INT_EQ(0, (int)net_emulator_pending(&emu));

    net_emulator_destroy(&emu);
    return 0;
}

/* ── Main ───────────────────────────────────────────────────────── */

int main(void) {
    printf("p103_net_emulator_tests:\n");

    RUN_TEST(test_default_config);
    RUN_TEST(test_init_destroy);
    RUN_TEST(test_init_null_safe);
    RUN_TEST(test_zero_delay_passthrough);
    RUN_TEST(test_fixed_delay);
    RUN_TEST(test_100pct_loss);
    RUN_TEST(test_partial_loss);
    RUN_TEST(test_uniform_jitter_range);
    RUN_TEST(test_normal_distribution);
    RUN_TEST(test_lognormal_distribution);
    RUN_TEST(test_reorder);
    RUN_TEST(test_enable_disable);
    RUN_TEST(test_reconfigure);
    RUN_TEST(test_queue_full);
    RUN_TEST(test_duplicate);
    RUN_TEST(test_oversized_rejected);

    printf("\n%d/%d tests passed\n", g_test_count - g_fail_count, g_test_count);
    return g_fail_count ? 1 : 0;
}
