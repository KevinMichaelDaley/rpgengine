#include <stdio.h>
#include <string.h>
#include <time.h>
#include "ferrum/ferrum.h"

static int tests_run = 0, tests_failed = 0;
static void expect_true(int cond, const char *name) { tests_run++; if (!cond) { tests_failed++; fprintf(stderr, "FAIL %s\n", name); } else { fprintf(stdout, "OK %s\n", name); } }

static size_t make_frame(uint32_t ch, uint32_t seq, const uint8_t *payload, uint16_t len, uint8_t *out) {
    memcpy(out + 0, &ch, sizeof(uint32_t));
    memcpy(out + 4, &seq, sizeof(uint32_t));
    memcpy(out + 8, &len, sizeof(uint16_t));
    memcpy(out + 10, payload, len);
    return (size_t)(10 + len);
}

static void test_udp_recv_to_topics(void) {
    fr_topic_channel_config_t tcfg = { .capacity = 16 };
    fr_topic_channel_t *topic1 = fr_topic_channel_create(&tcfg);
    expect_true(topic1 != NULL, "topic_create");

    fr_topic_channel_t *topics[1] = { topic1 };

    fr_client_rx_config_t cfg = {0};
    cfg.max_channels = 1;
    cfg.max_pending_per_channel = 16;
    cfg.topics = topics;
    cfg.num_topics = 1;
    fr_client_rx_t *rx = fr_client_rx_create(&cfg);
    expect_true(rx != NULL, "rx_create_udp");
    expect_true(fr_client_rx_bind_ipv4(rx, 127,0,0,1, 40081), "rx_bind");
    expect_true(fr_client_rx_start(rx), "rx_start");

    net_udp_socket_t sender; memset(&sender, 0, sizeof(sender));
    expect_true(net_udp_socket_open(&sender) == NET_UDP_SOCKET_OK, "sender_open");
    net_udp_addr_t to;
    expect_true(net_udp_addr_ipv4(&to, 127,0,0,1, 40081) == NET_UDP_SOCKET_OK, "to_addr");

    uint8_t buf1[64];
    const uint8_t p1[] = { 'T' };
    size_t f1 = make_frame(1, 1, p1, (uint16_t)sizeof(p1), buf1);
    expect_true(net_udp_socket_sendto(&sender, &to, buf1, f1) == NET_UDP_SOCKET_OK, "send_frame1");

    // Poll for delivery: thread scheduling can vary under load.
    uint8_t out[8];
    size_t out_len = sizeof(out);
    int got = 0;
    for (int i = 0; i < 200; ++i) {
        out_len = sizeof(out);
        if (fr_topic_channel_pop(topic1, out, &out_len)) {
            got = 1;
            break;
        }
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
        nanosleep(&ts, NULL);
    }

    expect_true(got, "topic_pop");
    if (got) {
        expect_true(out_len == 1 && out[0] == 'T', "topic_payload_T");
    }

    net_udp_socket_close(&sender);
    expect_true(fr_client_rx_stop(rx), "rx_stop");
    fr_client_rx_destroy(rx);
    fr_topic_channel_destroy(topic1);
}

int main(void) {
    fprintf(stdout, "RUN client_rx_udp_to_topics\n");
    test_udp_recv_to_topics();
    if (tests_failed == 0) { fprintf(stdout, "All %d tests passed\n", tests_run); return 0; }
    fprintf(stderr, "%d/%d tests failed\n", tests_failed, tests_run);
    return 1;
}
