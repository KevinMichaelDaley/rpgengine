#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ferrum/ferrum.h"
#include "ferrum/server/net/client_fiber.h"

static int test_in_order_to_topics(void) {
    fr_topic_channel_config_t tcfg = { .capacity = 16 };
    fr_topic_channel_t *topic = fr_topic_channel_create(&tcfg);
    assert(topic);

    fr_topic_channel_t *topics[1] = { topic };
    fr_server_client_fiber_config_t cfg = {0};
    cfg.reliable_channels = 1;
    cfg.reliable_slot_count = 16;
    cfg.max_payload_size = 64;
    cfg.topics = topics;
    cfg.num_topics = 1;

    fr_server_client_fiber_t *fib = fr_server_client_fiber_create(&cfg);
    assert(fib);

    /* Frame format: [seq:u16 LE][chan:u16 LE][payload] */
    uint8_t f1[4 + 1] = {1,0, 0,0, 'A'};
    uint8_t f2[4 + 1] = {2,0, 0,0, 'B'};
    uint8_t f3[4 + 1] = {3,0, 0,0, 'C'};
    assert(fr_server_client_fiber_inject_frame(fib, f1, sizeof f1));
    assert(fr_server_client_fiber_inject_frame(fib, f2, sizeof f2));
    assert(fr_server_client_fiber_inject_frame(fib, f3, sizeof f3));

    uint8_t out[8]; size_t len = sizeof out;
    assert(fr_topic_channel_pop(topic, out, &len));
    assert(len == 1 && out[0] == 'A');
    len = sizeof out;
    assert(fr_topic_channel_pop(topic, out, &len));
    assert(len == 1 && out[0] == 'B');
    len = sizeof out;
    assert(fr_topic_channel_pop(topic, out, &len));
    assert(len == 1 && out[0] == 'C');

    fr_server_client_fiber_destroy(fib);
    fr_topic_channel_destroy(topic);
    return 0;
}

static int test_out_of_order_to_topics(void) {
    fr_topic_channel_config_t tcfg = { .capacity = 16 };
    fr_topic_channel_t *topic = fr_topic_channel_create(&tcfg);
    assert(topic);
    fr_topic_channel_t *topics[1] = { topic };

    fr_server_client_fiber_config_t cfg = {0};
    cfg.reliable_channels = 1;
    cfg.reliable_slot_count = 16;
    cfg.max_payload_size = 64;
    cfg.topics = topics;
    cfg.num_topics = 1;
    fr_server_client_fiber_t *fib = fr_server_client_fiber_create(&cfg);
    assert(fib);

    uint8_t f2[4 + 1] = {2,0, 0,0, 'B'};
    uint8_t f1[4 + 1] = {1,0, 0,0, 'A'};
    assert(fr_server_client_fiber_inject_frame(fib, f2, sizeof f2));
    assert(fr_server_client_fiber_inject_frame(fib, f1, sizeof f1));

    uint8_t out[8]; size_t len = sizeof out;
    assert(fr_topic_channel_pop(topic, out, &len));
    assert(len == 1 && out[0] == 'A');
    len = sizeof out;
    assert(fr_topic_channel_pop(topic, out, &len));
    assert(len == 1 && out[0] == 'B');

    fr_server_client_fiber_destroy(fib);
    fr_topic_channel_destroy(topic);
    return 0;
}

static int test_invalid_frame_ignored(void) {
    fr_server_client_fiber_config_t cfg = {0};
    cfg.reliable_channels = 1;
    cfg.reliable_slot_count = 4;
    cfg.max_payload_size = 16;
    fr_server_client_fiber_t *fib = fr_server_client_fiber_create(&cfg);
    assert(fib);

    /* Too short (no header) */
    uint8_t bad[3] = {0};
    assert(!fr_server_client_fiber_inject_frame(fib, bad, sizeof bad));

    fr_server_client_fiber_destroy(fib);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_in_order_to_topics();
    rc |= test_out_of_order_to_topics();
    rc |= test_invalid_frame_ignored();
    printf("p008_server_client_fiber_stream_tests: OK\n");
    return rc;
}
