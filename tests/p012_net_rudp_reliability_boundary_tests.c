#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/rudp/peer.h"

#define TEST_FAIL(msg, ...)                                                                               \
    do {                                                                                                  \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);                    \
        return 1;                                                                                         \
    } while (0)

#define ASSERT_TRUE(cond)                                                                                 \
    do {                                                                                                  \
        if (!(cond)) {                                                                                    \
            TEST_FAIL("%s", #cond);                                                                      \
        }                                                                                                 \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                                                           \
    do {                                                                                                  \
        int _e = (int)(exp);                                                                              \
        int _a = (int)(act);                                                                              \
        if (_e != _a) {                                                                                   \
            TEST_FAIL("expected %d got %d", _e, _a);                                                     \
        }                                                                                                 \
    } while (0)

#define ASSERT_MEM_EQ(exp, act, n)                                                                        \
    do {                                                                                                  \
        if (memcmp((exp), (act), (n)) != 0) {                                                              \
            TEST_FAIL("%s", "memcmp failed");                                                          \
        }                                                                                                 \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct pkt {
    uint8_t bytes[NET_RUDP_MAX_PACKET_SIZE];
    size_t size;
};

struct cap {
    struct pkt pkts[32];
    size_t count;
};

static int capture_sendto(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    (void)to;
    if (!io_user || !data) {
        return -1;
    }
    struct cap *c = (struct cap *)io_user;
    if (c->count >= ARRAY_SIZE(c->pkts) || size > sizeof(c->pkts[0].bytes)) {
        return -1;
    }
    memcpy(c->pkts[c->count].bytes, data, size);
    c->pkts[c->count].size = size;
    c->count++;
    return 0;
}

static size_t used_send_slots(const net_rudp_peer_t *peer) {
    if (!peer || !peer->send_slots) {
        return 0u;
    }
    size_t used = 0u;
    for (size_t i = 0u; i < peer->send_slot_count; ++i) {
        if (peer->send_slots[i].used) {
            used++;
        }
    }
    return used;
}

static int deliver_packet(net_rudp_peer_t *peer, const struct pkt *p) {
    uint8_t reliable = 0u;
    uint16_t schema_id = 0u;
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
    size_t payload_size = 0u;

    int rc = net_rudp_peer_receive(peer, p->bytes, p->size, 0u, &reliable, &schema_id, payload, sizeof(payload), &payload_size);
    (void)reliable;
    (void)schema_id;
    (void)payload;
    (void)payload_size;
    return rc;
}

static int test_reliable_resends_until_acked(void) {
    net_rudp_send_slot_t a_slots[8];
    net_rudp_send_slot_t b_slots[8];
    memset(a_slots, 0, sizeof(a_slots));
    memset(b_slots, 0, sizeof(b_slots));

    net_rudp_peer_t a;
    net_rudp_peer_t b;
    net_rudp_peer_init_with_storage(&a, NET_RUDP_PROTOCOL_ID_P008, 10u, a_slots, ARRAY_SIZE(a_slots));
    net_rudp_peer_init_with_storage(&b, NET_RUDP_PROTOCOL_ID_P008, 10u, b_slots, ARRAY_SIZE(b_slots));

    struct cap a_to_b;
    memset(&a_to_b, 0, sizeof(a_to_b));

    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    const uint8_t payload[] = {0xA1u, 0xB2u, 0xC3u};
    uint16_t seq0 = 0xFFFFu;
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_reliable_via(&a, &a_to_b, capture_sendto, &dummy, 0u, 0x1234u, payload, sizeof(payload), &seq0));
    ASSERT_INT_EQ(1, (int)a_to_b.count);
    ASSERT_TRUE(used_send_slots(&a) == 1u);

    /* Receiver consumes packet; sender has not yet seen ACK. */
    ASSERT_INT_EQ(NET_RUDP_OK, deliver_packet(&b, &a_to_b.pkts[0]));

    /* After resend interval elapses, sender resends the same tracked packet. */
    ASSERT_INT_EQ(NET_RUDP_OK, net_rudp_peer_tick_resend_via(&a, &a_to_b, capture_sendto, &dummy, 11u));
    ASSERT_INT_EQ(2, (int)a_to_b.count);
    ASSERT_INT_EQ((int)a_to_b.pkts[0].size, (int)a_to_b.pkts[1].size);
    ASSERT_MEM_EQ(a_to_b.pkts[0].bytes, a_to_b.pkts[1].bytes, a_to_b.pkts[0].size);

    /* Receiver sends any packet back; its header must carry the ACK info. */
    struct cap b_to_a;
    memset(&b_to_a, 0, sizeof(b_to_a));
    const uint8_t ack_payload[] = {0x00u};
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_unreliable_via(&b, &b_to_a, capture_sendto, &dummy, 12u, 0x9999u, ack_payload,
                                                    sizeof(ack_payload)));
    ASSERT_INT_EQ(1, (int)b_to_a.count);

    /* Sender receives ACK-carrying packet and should retire its send slot. */
    ASSERT_INT_EQ(NET_RUDP_OK, deliver_packet(&a, &b_to_a.pkts[0]));
    ASSERT_TRUE(used_send_slots(&a) == 0u);

    /* No further resends after ACK retirement. */
    ASSERT_INT_EQ(NET_RUDP_OK, net_rudp_peer_tick_resend_via(&a, &a_to_b, capture_sendto, &dummy, 25u));
    ASSERT_INT_EQ(2, (int)a_to_b.count);
    (void)seq0;
    return 0;
}

static int test_ack_bits_retires_multiple_slots(void) {
    net_rudp_send_slot_t a_slots[8];
    net_rudp_send_slot_t b_slots[8];
    memset(a_slots, 0, sizeof(a_slots));
    memset(b_slots, 0, sizeof(b_slots));

    net_rudp_peer_t a;
    net_rudp_peer_t b;
    net_rudp_peer_init_with_storage(&a, NET_RUDP_PROTOCOL_ID_P008, 50u, a_slots, ARRAY_SIZE(a_slots));
    net_rudp_peer_init_with_storage(&b, NET_RUDP_PROTOCOL_ID_P008, 50u, b_slots, ARRAY_SIZE(b_slots));

    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    struct cap a_to_b;
    memset(&a_to_b, 0, sizeof(a_to_b));

    const uint8_t p0[] = {0x10u};
    const uint8_t p1[] = {0x20u};

    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_reliable_via(&a, &a_to_b, capture_sendto, &dummy, 0u, 0x1000u, p0, sizeof(p0), NULL));
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_reliable_via(&a, &a_to_b, capture_sendto, &dummy, 0u, 0x1001u, p1, sizeof(p1), NULL));
    ASSERT_INT_EQ(2, (int)a_to_b.count);
    ASSERT_TRUE(used_send_slots(&a) == 2u);

    /* Receiver receives both; its outgoing ACK (ack + ack_bits) should cover both sequences. */
    ASSERT_INT_EQ(NET_RUDP_OK, deliver_packet(&b, &a_to_b.pkts[0]));
    ASSERT_INT_EQ(NET_RUDP_OK, deliver_packet(&b, &a_to_b.pkts[1]));

    struct cap b_to_a;
    memset(&b_to_a, 0, sizeof(b_to_a));
    const uint8_t ack_payload[] = {0x01u};
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_peer_send_unreliable_via(&b, &b_to_a, capture_sendto, &dummy, 1u, 0x9999u, ack_payload,
                                                    sizeof(ack_payload)));
    ASSERT_INT_EQ(1, (int)b_to_a.count);

    ASSERT_INT_EQ(NET_RUDP_OK, deliver_packet(&a, &b_to_a.pkts[0]));
    ASSERT_TRUE(used_send_slots(&a) == 0u);
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"reliable_resends_until_acked", test_reliable_resends_until_acked},
        {"ack_bits_retires_multiple_slots", test_ack_bits_retires_multiple_slots},
    };

    for (size_t i = 0u; i < ARRAY_SIZE(tests); ++i) {
        int rc = tests[i].fn();
        if (rc != 0) {
            fprintf(stderr, "FAILED %s\n", tests[i].name);
            return 1;
        }
        fprintf(stdout, "OK %s\n", tests[i].name);
    }

    fprintf(stdout, "All %zu tests passed\n", ARRAY_SIZE(tests));
    return 0;
}
