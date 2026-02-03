#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
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

#define ASSERT_EQ_INT(expected, actual)                                                                   \
    do {                                                                                                  \
        long long _exp = (long long)(expected);                                                           \
        long long _act = (long long)(actual);                                                             \
        if (_exp != _act) {                                                                               \
            TEST_FAIL("expected %lld got %lld", _exp, _act);                                             \
        }                                                                                                 \
    } while (0)

#define ASSERT_EQ_MEM(expected, actual, size)                                                             \
    do {                                                                                                  \
        if (memcmp((expected), (actual), (size)) != 0) {                                                   \
            TEST_FAIL("%s", "memcmp failed");                                                          \
        }                                                                                                 \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

struct pkt {
    uint8_t bytes[NET_RUDP_MAX_PACKET_SIZE];
    size_t size;
};

struct net_capture {
    struct pkt pkts[64];
    size_t count;
};

static int capture_sendto(void *user, const net_udp_addr_t *to, const void *data, size_t size) {
    (void)to;
    struct net_capture *cap = (struct net_capture *)user;
    if (!cap || !data) {
        return -1;
    }
    if (cap->count >= ARRAY_SIZE(cap->pkts)) {
        return -1;
    }
    if (size > sizeof(cap->pkts[0].bytes)) {
        return -1;
    }
    memcpy(cap->pkts[cap->count].bytes, data, size);
    cap->pkts[cap->count].size = size;
    cap->count++;
    return 0;
}

static int test_send_large_ok_by_default(void) {
    net_rudp_send_slot_t slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, ARRAY_SIZE(slots));

    struct net_capture cap;
    memset(&cap, 0, sizeof(cap));

    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    uint8_t payload[4096];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(i & 0xFFu);
    }

    int rc = net_rudp_peer_send_unreliable_via(&peer, &cap, capture_sendto, &dummy, 1u, 123u, payload, sizeof(payload));
    ASSERT_EQ_INT(NET_RUDP_OK, rc);
    ASSERT_TRUE(cap.count > 1u);

    return 0;
}

static int test_fragmented_unreliable_roundtrip(void) {
    net_rudp_send_slot_t a_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    net_rudp_send_slot_t b_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(a_slots, 0, sizeof(a_slots));
    memset(b_slots, 0, sizeof(b_slots));

    net_rudp_peer_t a;
    net_rudp_peer_t b;
    net_rudp_peer_init_with_storage(&a, NET_RUDP_PROTOCOL_ID_P008, 50u, a_slots, ARRAY_SIZE(a_slots));
    net_rudp_peer_init_with_storage(&b, NET_RUDP_PROTOCOL_ID_P008, 50u, b_slots, ARRAY_SIZE(b_slots));

    /* Default is enabled; keep explicit enable calls as a no-op override. */
    net_rudp_peer_enable_fragmentation(&a, 1, NULL, 0);
    net_rudp_peer_enable_fragmentation(&b, 1, NULL, 0);

    struct net_capture cap;
    memset(&cap, 0, sizeof(cap));

    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    uint8_t payload[2000];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)((i * 3u) & 0xFFu);
    }

    int src = net_rudp_peer_send_unreliable_via(&a, &cap, capture_sendto, &dummy, 1u, 77u, payload, sizeof(payload));
    ASSERT_EQ_INT(NET_RUDP_OK, src);
    ASSERT_TRUE(cap.count > 1u);

    uint8_t out_payload[4096];
    size_t out_size = 0u;
    uint16_t out_schema = 0u;
    uint8_t out_rel = 0u;

    int got = 0;
    for (size_t i = 0; i < cap.count; ++i) {
        int rc = net_rudp_peer_receive(&b,
                                       cap.pkts[i].bytes,
                                       cap.pkts[i].size,
                                       &out_rel,
                                       &out_schema,
                                       out_payload,
                                       sizeof(out_payload),
                                       &out_size);
        if (rc == NET_RUDP_OK) {
            got++;
        } else {
            ASSERT_TRUE(rc == NET_RUDP_EMPTY);
        }
    }

    ASSERT_EQ_INT(1, got);
    ASSERT_EQ_INT(77, out_schema);
    ASSERT_EQ_INT(0, out_rel);
    ASSERT_EQ_INT((int)sizeof(payload), (int)out_size);
    ASSERT_EQ_MEM(payload, out_payload, sizeof(payload));

    return 0;
}

static int test_fragmented_unreliable_out_of_order(void) {
    net_rudp_send_slot_t a_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    net_rudp_send_slot_t b_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(a_slots, 0, sizeof(a_slots));
    memset(b_slots, 0, sizeof(b_slots));

    net_rudp_peer_t a;
    net_rudp_peer_t b;
    net_rudp_peer_init_with_storage(&a, NET_RUDP_PROTOCOL_ID_P008, 50u, a_slots, ARRAY_SIZE(a_slots));
    net_rudp_peer_init_with_storage(&b, NET_RUDP_PROTOCOL_ID_P008, 50u, b_slots, ARRAY_SIZE(b_slots));

    net_rudp_peer_enable_fragmentation(&a, 1, NULL, 0);
    net_rudp_peer_enable_fragmentation(&b, 1, NULL, 0);

    struct net_capture cap;
    memset(&cap, 0, sizeof(cap));

    net_udp_addr_t dummy;
    memset(&dummy, 0, sizeof(dummy));

    uint8_t payload[2000];
    for (size_t i = 0; i < sizeof(payload); ++i) {
        payload[i] = (uint8_t)(0xA5u ^ (uint8_t)i);
    }

    ASSERT_EQ_INT(NET_RUDP_OK,
                  net_rudp_peer_send_unreliable_via(&a, &cap, capture_sendto, &dummy, 1u, 99u, payload, sizeof(payload)));
    ASSERT_TRUE(cap.count > 1u);

    uint8_t out_payload[4096];
    size_t out_size = 0u;
    uint16_t out_schema = 0u;
    uint8_t out_rel = 0u;

    int got = 0;

    /* Deliver in reverse order. */
    for (size_t i = cap.count; i > 0u; --i) {
        int rc = net_rudp_peer_receive(&b,
                                       cap.pkts[i - 1u].bytes,
                                       cap.pkts[i - 1u].size,
                                       &out_rel,
                                       &out_schema,
                                       out_payload,
                                       sizeof(out_payload),
                                       &out_size);
        if (rc == NET_RUDP_OK) {
            got++;
        } else {
            ASSERT_TRUE(rc == NET_RUDP_EMPTY);
        }
    }

    ASSERT_EQ_INT(1, got);
    ASSERT_EQ_INT(99, out_schema);
    ASSERT_EQ_INT(0, out_rel);
    ASSERT_EQ_INT((int)sizeof(payload), (int)out_size);
    ASSERT_EQ_MEM(payload, out_payload, sizeof(payload));

    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"send_large_ok_by_default", test_send_large_ok_by_default},
        {"fragmented_unreliable_roundtrip", test_fragmented_unreliable_roundtrip},
        {"fragmented_unreliable_out_of_order", test_fragmented_unreliable_out_of_order},
    };

    for (size_t i = 0; i < ARRAY_SIZE(tests); ++i) {
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
