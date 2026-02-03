#include <stdio.h>
#include <string.h>
#include <assert.h>

#include "ferrum/net/stream.h"
#include "ferrum/net/topic_channel.h"

static int test_channel_ids_and_topic_pump(void) {
    fr_topic_channel_config_t tc = { .capacity = 8 };
    fr_topic_channel_t *topics[2] = {
        fr_topic_channel_create(&tc),
        fr_topic_channel_create(&tc)
    };
    assert(topics[0] && topics[1]);

    fr_rudp_stream_config_t cfg = {0};
    cfg.reliable_channels = 2;
    cfg.reliable_slot_count = 8;
    cfg.max_payload_size = 32;
    cfg.topics = topics;
    cfg.num_topics = 2;
    fr_rudp_stream_t *s = fr_rudp_stream_create(&cfg);
    assert(s != NULL);

    /* Frame format: 2-byte seq LE, 2-byte chan LE, payload */
    uint8_t f1c0[4 + 3] = {1, 0, 0, 0, 'A','A','A'};
    uint8_t f1c1[4 + 2] = {1, 0, 1, 0, 'B','B'};
    uint8_t f1c1_dup[4 + 2] = {1, 0, 1, 0, 'X','X'};

    assert(fr_rudp_stream_push_frame(s, f1c0, sizeof f1c0));
    assert(fr_rudp_stream_push_frame(s, f1c1, sizeof f1c1));
    /* Duplicate should not result in second delivery. */
    (void)fr_rudp_stream_push_frame(s, f1c1_dup, sizeof f1c1_dup);

    /* Pop from topics: channel 0 then 1 */
    uint8_t out[16];
    size_t cap = sizeof out;
    assert(fr_topic_channel_pop(topics[0], out, &cap));
    assert(cap == 3);
    assert(out[0]=='A' && out[1]=='A' && out[2]=='A');

    cap = sizeof out;
    assert(fr_topic_channel_pop(topics[1], out, &cap));
    assert(cap == 2);
    assert(out[0]=='B' && out[1]=='B');

    /* Out-of-order for channel 0: seq 2 then 3 (deliver only after 2 then 3). */
    uint8_t f2c0[4 + 3] = {2, 0, 0, 0, 'C','C','C'};
    uint8_t f3c0[4 + 3] = {3, 0, 0, 0, 'D','D','D'};
    assert(fr_rudp_stream_push_frame(s, f3c0, sizeof f3c0));
    assert(fr_rudp_stream_push_frame(s, f2c0, sizeof f2c0));

    cap = sizeof out;
    assert(fr_topic_channel_pop(topics[0], out, &cap));
    assert(cap == 3 && out[0]=='C' && out[1]=='C' && out[2]=='C');
    cap = sizeof out;
    assert(fr_topic_channel_pop(topics[0], out, &cap));
    assert(cap == 3 && out[0]=='D' && out[1]=='D' && out[2]=='D');

    /* Empty pops: should return false. */
    cap = sizeof out;
    assert(!fr_topic_channel_pop(topics[0], out, &cap));
    cap = sizeof out;
    assert(!fr_topic_channel_pop(topics[1], out, &cap));

    fr_rudp_stream_destroy(s);
    fr_topic_channel_destroy(topics[0]);
    fr_topic_channel_destroy(topics[1]);
    return 0;
}

int main(void) {
    int rc = 0;
    rc |= test_channel_ids_and_topic_pump();
    printf("p007_net_stream_channel_topic_tests: OK\n");
    return rc;
}
