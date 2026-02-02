// Phase 1: Tests first (RED)
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "ferrum/ferrum.h"
#include "ferrum/net/client/runtime_rx.h"

static int tests_run = 0;
static int tests_failed = 0;

static void expect_true(bool cond, const char *name) {
    tests_run++;
    if (!cond) {
        tests_failed++;
        fprintf(stderr, "FAIL %s\n", name);
    } else {
        fprintf(stdout, "OK %s\n", name);
    }
}

// A tiny fake frame format for tests:
// [channel_id:u32][seq:u32][len:u16][payload:len]
static size_t make_frame(uint32_t ch, uint32_t seq, const uint8_t *payload, uint16_t len, uint8_t *out) {
    memcpy(out + 0, &ch, sizeof(uint32_t));
    memcpy(out + 4, &seq, sizeof(uint32_t));
    memcpy(out + 8, &len, sizeof(uint16_t));
    memcpy(out + 10, payload, len);
    return (size_t)(10 + len);
}

static void test_in_order_delivery(void) {
    fr_client_rx_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    fr_client_rx_t *rx = fr_client_rx_create(&cfg);
    expect_true(rx != NULL, "rx_create");

    uint8_t frame1[128], frame2[128], frame3[128];
    const uint8_t p1[] = { 'A' };
    const uint8_t p2[] = { 'B' };
    const uint8_t p3[] = { 'C' };

    size_t f1 = make_frame(1, 1, p1, (uint16_t)sizeof(p1), frame1);
    size_t f2 = make_frame(1, 2, p2, (uint16_t)sizeof(p2), frame2);
    size_t f3 = make_frame(1, 3, p3, (uint16_t)sizeof(p3), frame3);

    expect_true(fr_client_rx_inject(rx, frame1, f1), "inject_1");
    expect_true(fr_client_rx_inject(rx, frame2, f2), "inject_2");
    expect_true(fr_client_rx_inject(rx, frame3, f3), "inject_3");

    uint8_t out[8];
    size_t out_len = sizeof(out);
    expect_true(fr_client_rx_pop_message(rx, 1, out, &out_len), "pop_1");
    expect_true(out_len == 1 && out[0] == 'A', "payload_A");
    out_len = sizeof(out);
    expect_true(fr_client_rx_pop_message(rx, 1, out, &out_len), "pop_2");
    expect_true(out_len == 1 && out[0] == 'B', "payload_B");
    out_len = sizeof(out);
    expect_true(fr_client_rx_pop_message(rx, 1, out, &out_len), "pop_3");
    expect_true(out_len == 1 && out[0] == 'C', "payload_C");

    fr_client_rx_destroy(rx);
}

static void test_out_of_order_and_duplicates(void) {
    fr_client_rx_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    fr_client_rx_t *rx = fr_client_rx_create(&cfg);
    expect_true(rx != NULL, "rx_create2");

    uint8_t frame_a[128], frame_b[128], frame_c[128], frame_dup[128];
    const uint8_t p1[] = { 'X' };
    const uint8_t p2[] = { 'Y' };
    const uint8_t p3[] = { 'Z' };

    size_t f2 = make_frame(2, 2, p2, (uint16_t)sizeof(p2), frame_b);
    size_t f1 = make_frame(2, 1, p1, (uint16_t)sizeof(p1), frame_a);
    size_t f1_dup = make_frame(2, 1, p1, (uint16_t)sizeof(p1), frame_dup);
    size_t f3 = make_frame(2, 3, p3, (uint16_t)sizeof(p3), frame_c);

    expect_true(fr_client_rx_inject(rx, frame_b, f2), "inject_2_first");
    expect_true(fr_client_rx_inject(rx, frame_a, f1), "inject_1_after");
    expect_true(fr_client_rx_inject(rx, frame_dup, f1_dup), "inject_1_dup");
    expect_true(fr_client_rx_inject(rx, frame_c, f3), "inject_3");

    uint8_t out[8];
    size_t out_len = sizeof(out);
    expect_true(fr_client_rx_pop_message(rx, 2, out, &out_len), "pop_1_ordered");
    expect_true(out_len == 1 && out[0] == 'X', "payload_X");
    out_len = sizeof(out);
    expect_true(fr_client_rx_pop_message(rx, 2, out, &out_len), "pop_2_ordered");
    expect_true(out_len == 1 && out[0] == 'Y', "payload_Y");
    out_len = sizeof(out);
    expect_true(fr_client_rx_pop_message(rx, 2, out, &out_len), "pop_3_ordered");
    expect_true(out_len == 1 && out[0] == 'Z', "payload_Z");

    // No more messages
    out_len = sizeof(out);
    expect_true(!fr_client_rx_pop_message(rx, 2, out, &out_len), "pop_none");

    fr_client_rx_destroy(rx);
}

int main(void) {
    fprintf(stdout, "RUN client_rx_in_order\n");
    test_in_order_delivery();
    fprintf(stdout, "RUN client_rx_out_of_order\n");
    test_out_of_order_and_duplicates();

    if (tests_failed == 0) {
        fprintf(stdout, "All %d tests passed\n", tests_run);
        return 0;
    }
    fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
    return 1;
}
