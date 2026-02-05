#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/reliability_send.h"
#include "ferrum/net/rudp/wire_frame.h"

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

#define ASSERT_U32_EQ(exp, act)                                                                           \
    do {                                                                                                  \
        uint32_t _e = (uint32_t)(exp);                                                                    \
        uint32_t _a = (uint32_t)(act);                                                                    \
        if (_e != _a) {                                                                                   \
            TEST_FAIL("expected %u got %u", (unsigned)_e, (unsigned)_a);                                 \
        }                                                                                                 \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct pkt {
    uint8_t bytes[NET_RUDP_MAX_PACKET_SIZE];
    size_t size;
};

struct cap {
    struct pkt pkts[64];
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

static int decode_header(const struct pkt *p, net_packet_header_t *out_header, net_rudp_wire_frame_view_t *out_frame) {
    if (!p || !out_header || !out_frame) {
        return -1;
    }
    int rc = net_rudp_wire_decode(out_header, out_frame, p->bytes, p->size);
    return (rc == NET_RUDP_WIRE_OK) ? 0 : -1;
}

static int test_unreliable_does_not_advance_sequence(void) {
    net_rudp_send_slot_t slots[4];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, ARRAY_SIZE(slots));

    struct cap c;
    memset(&c, 0, sizeof(c));
    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    const uint8_t payload[] = {0x01u, 0x02u};

    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_send_unreliable_via(&peer, &c, capture_sendto, &dummy, 0u, 0x1111u, payload, sizeof(payload)));
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_send_unreliable_via(&peer, &c, capture_sendto, &dummy, 1u, 0x1111u, payload, sizeof(payload)));
    ASSERT_INT_EQ(2, (int)c.count);

    net_packet_header_t h0;
    net_packet_header_t h1;
    net_rudp_wire_frame_view_t f0;
    net_rudp_wire_frame_view_t f1;
    memset(&h0, 0, sizeof(h0));
    memset(&h1, 0, sizeof(h1));
    memset(&f0, 0, sizeof(f0));
    memset(&f1, 0, sizeof(f1));

    ASSERT_INT_EQ(0, decode_header(&c.pkts[0], &h0, &f0));
    ASSERT_INT_EQ(0, decode_header(&c.pkts[1], &h1, &f1));

    ASSERT_INT_EQ((int)h0.sequence, (int)h1.sequence);
    ASSERT_INT_EQ(0, (int)(f0.flags & NET_RUDP_WIRE_FLAG_RELIABLE));
    ASSERT_INT_EQ(0, (int)(f1.flags & NET_RUDP_WIRE_FLAG_RELIABLE));
    return 0;
}

static int test_reliable_includes_current_ack_state(void) {
    net_rudp_send_slot_t slots[4];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, ARRAY_SIZE(slots));

    /* Drive recv window state so outgoing header has ack + ack_bits. */
    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&peer.recv_window, 100u));
    ASSERT_INT_EQ(NET_ACK_WINDOW_OK, net_ack_window_receive(&peer.recv_window, 101u));

    const uint16_t expect_ack = net_ack_window_ack(&peer.recv_window);
    const uint32_t expect_bits = net_ack_window_ack_bits(&peer.recv_window);

    struct cap c;
    memset(&c, 0, sizeof(c));
    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    const uint8_t payload[] = {0xAAu};
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_send_reliable_via(&peer, &c, capture_sendto, &dummy, 5u, 0x2222u, payload, sizeof(payload), NULL));
    ASSERT_INT_EQ(1, (int)c.count);

    net_packet_header_t header;
    net_rudp_wire_frame_view_t frame;
    memset(&header, 0, sizeof(header));
    memset(&frame, 0, sizeof(frame));

    ASSERT_INT_EQ(0, decode_header(&c.pkts[0], &header, &frame));
    ASSERT_INT_EQ((int)NET_RUDP_PROTOCOL_ID_P008, (int)header.protocol_id);
    ASSERT_INT_EQ((int)expect_ack, (int)header.ack);
    ASSERT_U32_EQ(expect_bits, header.ack_bits);
    ASSERT_TRUE((frame.flags & NET_RUDP_WIRE_FLAG_RELIABLE) != 0u);
    return 0;
}

static int test_tick_resend_resends_tracked_packet(void) {
    net_rudp_send_slot_t slots[2];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 10u, slots, ARRAY_SIZE(slots));

    struct cap c;
    memset(&c, 0, sizeof(c));
    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    const uint8_t payload[] = {0x10u, 0x20u, 0x30u};
    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_send_reliable_via(&peer, &c, capture_sendto, &dummy, 0u, 0x3333u, payload, sizeof(payload), NULL));
    ASSERT_INT_EQ(1, (int)c.count);

    /* Not yet past resend interval. */
    ASSERT_INT_EQ(NET_RUDP_OK, net_rudp_reliability_tick_resend_via(&peer, &c, capture_sendto, &dummy, 5u));
    ASSERT_INT_EQ(1, (int)c.count);

    /* Past resend interval => identical bytes resent. */
    ASSERT_INT_EQ(NET_RUDP_OK, net_rudp_reliability_tick_resend_via(&peer, &c, capture_sendto, &dummy, 11u));
    ASSERT_INT_EQ(2, (int)c.count);
    ASSERT_INT_EQ((int)c.pkts[0].size, (int)c.pkts[1].size);
    ASSERT_TRUE(memcmp(c.pkts[0].bytes, c.pkts[1].bytes, c.pkts[0].size) == 0);
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"unreliable_does_not_advance_sequence", test_unreliable_does_not_advance_sequence},
        {"reliable_includes_current_ack_state", test_reliable_includes_current_ack_state},
        {"tick_resend_resends_tracked_packet", test_tick_resend_resends_tracked_packet},
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
