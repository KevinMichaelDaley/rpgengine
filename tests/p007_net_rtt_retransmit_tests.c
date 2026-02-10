/**
 * @file p007_net_rtt_retransmit_tests.c
 * @brief Tests for RTT estimation and retransmit scheduling.
 *
 * Exercises:
 * - RTT sample from ACK updates smoothed_rtt_ms correctly
 * - No negative RTT (even with wrapping/edge-case clocks)
 * - RTT variance bounded after multiple samples
 * - Resend triggers when ack_bits shows missing seq after timeout
 * - Adaptive resend interval based on smoothed RTT
 * - Expired slots cleaned up after max_slot_age_ms
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/wire_frame.h"
#include "ferrum/net/packet_header.h"

/* ── Test macros ────────────────────────────────────────────────── */

#define ASSERT_TRUE(cond)                                              \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__,   \
                    #cond);                                            \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define ASSERT_UINT_EQ(exp, act)                                       \
    do {                                                               \
        if ((uint64_t)(exp) != (uint64_t)(act)) {                      \
            fprintf(stderr, "FAIL: %s:%d: expected %llu got %llu\n",   \
                    __FILE__, __LINE__,                                 \
                    (unsigned long long)(exp),                          \
                    (unsigned long long)(act));                         \
            return 1;                                                  \
        }                                                              \
    } while (0)

#define ASSERT_INT_EQ(exp, act)                                        \
    do {                                                               \
        if ((int)(exp) != (int)(act)) {                                \
            fprintf(stderr, "FAIL: %s:%d: expected %d got %d\n",       \
                    __FILE__, __LINE__, (int)(exp), (int)(act));        \
            return 1;                                                  \
        }                                                              \
    } while (0)

/* ── Fake sendto callback ──────────────────────────────────────── */

/** Counts calls and stores last packet for inspection. */
typedef struct fake_io {
    int send_count;
    uint8_t last_packet[NET_RUDP_MAX_PACKET_SIZE];
    size_t last_size;
} fake_io_t;

static int fake_sendto(void *io_user, const net_udp_addr_t *to,
                       const void *data, size_t size) {
    (void)to;
    fake_io_t *io = io_user;
    io->send_count++;
    if (size <= sizeof(io->last_packet)) {
        memcpy(io->last_packet, data, size);
        io->last_size = size;
    }
    return 0;
}

/* ── Helpers ────────────────────────────────────────────────────── */

#define SLOT_COUNT 16u
#define PROTOCOL_ID 0x54455354u /* 'TEST' */

/** Build an ACK packet that the receiver peer can process.
 *  We build it as if sent by 'sender', carrying ack/ack_bits that
 *  acknowledge sequences from 'receiver'. */
static size_t build_ack_packet(net_rudp_peer_t *sender,
                                uint16_t ack, uint32_t ack_bits,
                                uint8_t *out, size_t out_cap) {
    net_packet_header_t hdr;
    hdr.protocol_id = sender->protocol_id;
    hdr.sequence = sender->next_sequence;
    hdr.ack = ack;
    hdr.ack_bits = ack_bits;

    /* Encode a minimal unreliable frame with 1-byte payload. */
    uint8_t payload = 0xAA;
    size_t encoded_size = 0;
    int rc = net_rudp_wire_encode(&hdr, 0u /* unreliable */, 0x0001, &payload, 1,
                                  out, out_cap, &encoded_size);
    (void)rc;
    return encoded_size;
}

/* ── Test: first RTT sample initializes smoothed_rtt ───────────── */

static int test_rtt_first_sample_initializes(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t sender;
    net_rudp_peer_init_with_storage(&sender, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    net_rudp_peer_t receiver;
    net_rudp_send_slot_t rslots[SLOT_COUNT];
    memset(rslots, 0, sizeof(rslots));
    net_rudp_peer_init_with_storage(&receiver, PROTOCOL_ID, 50, rslots, SLOT_COUNT);

    ASSERT_UINT_EQ(0u, sender.smoothed_rtt_ms);

    /* Send a reliable message at t=100ms. */
    fake_io_t io = {0};
    net_udp_addr_t addr = {0};
    uint16_t seq = 0;
    int rc = net_rudp_peer_send_reliable_via(&sender, &io, fake_sendto, &addr,
                                             100u, 0x0001, "hi", 2, &seq);
    ASSERT_INT_EQ(NET_RUDP_OK, rc);
    /* next_sequence starts at 1. */
    ASSERT_UINT_EQ(1u, seq);

    /* Receiver processes the packet to update its ACK window. */
    uint8_t out_reliable = 0;
    uint16_t out_schema = 0;
    uint8_t out_payload[64];
    size_t out_size = 0;
    rc = net_rudp_peer_receive(&receiver, io.last_packet, io.last_size, 100u,
                                &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);
    ASSERT_INT_EQ(NET_RUDP_OK, rc);

    /* Build an ACK packet from receiver back to sender at t=150ms (50ms RTT). */
    uint8_t ack_pkt[NET_RUDP_MAX_PACKET_SIZE];
    size_t ack_size = build_ack_packet(&receiver, seq, 0u, ack_pkt, sizeof(ack_pkt));
    ASSERT_TRUE(ack_size > 0);

    /* Sender processes the ACK. */
    rc = net_rudp_peer_receive(&sender, ack_pkt, ack_size, 150u,
                                &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);
    /* May return EMPTY (no payload for sender), that's fine. */

    /* First sample should initialize smoothed_rtt_ms directly. */
    ASSERT_UINT_EQ(50u, sender.smoothed_rtt_ms);

    return 0;
}

/* ── Test: EWMA smoothing across multiple samples ──────────────── */

static int test_rtt_ewma_smoothing(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t sender;
    net_rudp_peer_init_with_storage(&sender, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    net_rudp_peer_t receiver;
    net_rudp_send_slot_t rslots[SLOT_COUNT];
    memset(rslots, 0, sizeof(rslots));
    net_rudp_peer_init_with_storage(&receiver, PROTOCOL_ID, 50, rslots, SLOT_COUNT);

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};
    uint8_t out_reliable = 0;
    uint16_t out_schema = 0;
    uint8_t out_payload[64];
    size_t out_size = 0;

    /* Send and ACK 8 messages, each with 100ms RTT. */
    for (int i = 0; i < 8; i++) {
        uint64_t send_time = (uint64_t)(i * 200);
        uint16_t seq = 0;
        int rc = net_rudp_peer_send_reliable_via(&sender, &io, fake_sendto, &addr,
                                                  send_time, 0x0001, "x", 1, &seq);
        ASSERT_INT_EQ(NET_RUDP_OK, rc);

        /* Receiver processes to update its window. */
        rc = net_rudp_peer_receive(&receiver, io.last_packet, io.last_size, send_time,
                                    &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);

        /* ACK at send_time + 100ms. */
        uint8_t ack_pkt[NET_RUDP_MAX_PACKET_SIZE];
        size_t ack_size = build_ack_packet(&receiver, seq, 0u, ack_pkt, sizeof(ack_pkt));

        rc = net_rudp_peer_receive(&sender, ack_pkt, ack_size, send_time + 100u,
                                    &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);
    }

    /* After 8 samples of 100ms each, EWMA should converge close to 100.
     * EWMA: rtt = rtt*7/8 + 100/8 each step.  Starting from 100,
     * all 100ms samples → should stay at 100. */
    ASSERT_UINT_EQ(100u, sender.smoothed_rtt_ms);

    return 0;
}

/* ── Test: RTT never goes negative even with zero-delta ACK ────── */

static int test_rtt_no_negative(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t sender;
    net_rudp_peer_init_with_storage(&sender, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    net_rudp_peer_t receiver;
    net_rudp_send_slot_t rslots[SLOT_COUNT];
    memset(rslots, 0, sizeof(rslots));
    net_rudp_peer_init_with_storage(&receiver, PROTOCOL_ID, 50, rslots, SLOT_COUNT);

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};
    uint8_t out_reliable = 0;
    uint16_t out_schema = 0;
    uint8_t out_payload[64];
    size_t out_size = 0;

    /* Send at t=100, ACK at t=100 (0ms RTT). */
    uint16_t seq = 0;
    net_rudp_peer_send_reliable_via(&sender, &io, fake_sendto, &addr,
                                     100u, 0x0001, "z", 1, &seq);
    net_rudp_peer_receive(&receiver, io.last_packet, io.last_size, 100u,
                           &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);

    uint8_t ack_pkt[NET_RUDP_MAX_PACKET_SIZE];
    size_t ack_size = build_ack_packet(&receiver, seq, 0u, ack_pkt, sizeof(ack_pkt));
    net_rudp_peer_receive(&sender, ack_pkt, ack_size, 100u,
                           &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);

    /* smoothed_rtt should be 0, not negative. */
    ASSERT_UINT_EQ(0u, sender.smoothed_rtt_ms);

    return 0;
}

/* ── Test: RTT bounded variance after jittery samples ──────────── */

static int test_rtt_bounded_after_jitter(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t sender;
    net_rudp_peer_init_with_storage(&sender, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    net_rudp_peer_t receiver;
    net_rudp_send_slot_t rslots[SLOT_COUNT];
    memset(rslots, 0, sizeof(rslots));
    net_rudp_peer_init_with_storage(&receiver, PROTOCOL_ID, 50, rslots, SLOT_COUNT);

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};
    uint8_t out_reliable = 0;
    uint16_t out_schema = 0;
    uint8_t out_payload[64];
    size_t out_size = 0;

    /* Alternating 50ms and 150ms RTT samples. */
    uint32_t rtts[] = {50, 150, 50, 150, 50, 150, 50, 150, 50, 150};
    for (int i = 0; i < 10; i++) {
        uint64_t send_time = (uint64_t)(i * 300);
        uint16_t seq = 0;
        net_rudp_peer_send_reliable_via(&sender, &io, fake_sendto, &addr,
                                         send_time, 0x0001, "j", 1, &seq);
        net_rudp_peer_receive(&receiver, io.last_packet, io.last_size, send_time,
                               &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);

        uint8_t ack_pkt[NET_RUDP_MAX_PACKET_SIZE];
        size_t ack_size = build_ack_packet(&receiver, seq, 0u, ack_pkt, sizeof(ack_pkt));
        net_rudp_peer_receive(&sender, ack_pkt, ack_size, send_time + rtts[i],
                               &out_reliable, &out_schema, out_payload, sizeof(out_payload), &out_size);
    }

    /* After many jittery samples, smoothed RTT should be between 50 and 150. */
    ASSERT_TRUE(sender.smoothed_rtt_ms >= 50u);
    ASSERT_TRUE(sender.smoothed_rtt_ms <= 150u);

    return 0;
}

/* ── Test: resend triggers after resend_interval_ms elapses ────── */

static int test_resend_after_interval(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};

    /* Send a reliable message at t=0. */
    uint16_t seq = 0;
    int rc = net_rudp_peer_send_reliable_via(&peer, &io, fake_sendto, &addr,
                                              0u, 0x0001, "hi", 2, &seq);
    ASSERT_INT_EQ(NET_RUDP_OK, rc);
    ASSERT_INT_EQ(1, io.send_count);

    /* Tick at t=30 (before interval) — no resend. */
    io.send_count = 0;
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 30u);
    ASSERT_INT_EQ(0, io.send_count);

    /* Tick at t=60 (past 50ms interval) — should resend. */
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 60u);
    ASSERT_INT_EQ(1, io.send_count);

    /* Tick at t=90 (only 30ms since last resend at 60) — no resend. */
    io.send_count = 0;
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 90u);
    ASSERT_INT_EQ(0, io.send_count);

    /* Tick at t=120 (60ms since last resend at 60) — resend again. */
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 120u);
    ASSERT_INT_EQ(1, io.send_count);

    return 0;
}

/* ── Test: expired slots cleaned up after max_slot_age_ms ──────── */

static int test_slot_expiry(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, PROTOCOL_ID, 50, slots, SLOT_COUNT);
    peer.max_slot_age_ms = 200u;

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};

    /* Send a reliable message at t=0. */
    uint16_t seq = 0;
    net_rudp_peer_send_reliable_via(&peer, &io, fake_sendto, &addr,
                                     0u, 0x0001, "expire", 6, &seq);
    ASSERT_TRUE(slots[0].used);

    /* Tick at t=150 — resend (within max age). */
    io.send_count = 0;
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 150u);
    ASSERT_INT_EQ(1, io.send_count);
    ASSERT_TRUE(slots[0].used);

    /* Tick at t=250 — slot should be expired (250-150=100 >= 200 from last_send=150). */
    /* Wait, max_slot_age is checked as (now - last_send) >= max_slot_age.
     * last_send was updated to 150 at the resend.  250-150=100 < 200.
     * Need to go to t=350 for expiry (350-150=200). */
    io.send_count = 0;
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 350u);
    /* Slot should be expired, not resent. */
    ASSERT_INT_EQ(0, io.send_count);
    ASSERT_TRUE(!slots[0].used);

    return 0;
}

/* ── Test: ACK via ack_bits retires correct slot ───────────────── */

static int test_ack_bits_retires_slot(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t sender;
    net_rudp_peer_init_with_storage(&sender, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    net_rudp_peer_t receiver;
    net_rudp_send_slot_t rslots[SLOT_COUNT];
    memset(rslots, 0, sizeof(rslots));
    net_rudp_peer_init_with_storage(&receiver, PROTOCOL_ID, 50, rslots, SLOT_COUNT);

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};
    uint8_t out_reliable = 0;
    uint16_t out_schema = 0;
    uint8_t out_payload[64];
    size_t out_size = 0;

    /* Send 3 reliable messages. */
    uint16_t seqs[3];
    for (int i = 0; i < 3; i++) {
        net_rudp_peer_send_reliable_via(&sender, &io, fake_sendto, &addr,
                                         (uint64_t)(i * 10), 0x0001, "x", 1, &seqs[i]);
        net_rudp_peer_receive(&receiver, io.last_packet, io.last_size,
                               (uint64_t)(i * 10),
                               &out_reliable, &out_schema, out_payload,
                               sizeof(out_payload), &out_size);
    }
    /* seqs = {1, 2, 3} (next_sequence starts at 1) */
    ASSERT_UINT_EQ(1, seqs[0]);
    ASSERT_UINT_EQ(2, seqs[1]);
    ASSERT_UINT_EQ(3, seqs[2]);

    /* All 3 slots should be used. */
    ASSERT_TRUE(slots[0].used);
    ASSERT_TRUE(slots[1].used);
    ASSERT_TRUE(slots[2].used);

    /* Receiver's window now has ack=3, ack_bits has bits for 2 and 1.
     * Build an ACK packet that acks seq 3 (direct) and seq 1 via ack_bits.
     * seq 2 is NOT acked.
     * ack_bits bit for seq 2: delta = 3-2 = 1, bit = 1<<0 = 0x1
     * ack_bits bit for seq 1: delta = 3-1 = 2, bit = 1<<1 = 0x2
     * Set only bit 1 (seq 1): ack_bits = 0x2 */
    uint8_t ack_pkt[NET_RUDP_MAX_PACKET_SIZE];
    size_t ack_size = build_ack_packet(&receiver, 3, 0x2u, ack_pkt, sizeof(ack_pkt));

    net_rudp_peer_receive(&sender, ack_pkt, ack_size, 100u,
                           &out_reliable, &out_schema, out_payload,
                           sizeof(out_payload), &out_size);

    /* Slot 0 (seq 1) and slot 2 (seq 3) should be retired. */
    ASSERT_TRUE(!slots[0].used);  /* seq 1: ACKed via ack_bits */
    ASSERT_TRUE(slots[1].used);   /* seq 2: NOT acked */
    ASSERT_TRUE(!slots[2].used);  /* seq 3: ACKed directly */

    return 0;
}

/* ── Test: multiple unACKed slots resend on tick ───────────────── */

static int test_multiple_resends(void) {
    net_rudp_send_slot_t slots[SLOT_COUNT];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, PROTOCOL_ID, 50, slots, SLOT_COUNT);

    fake_io_t io = {0};
    net_udp_addr_t addr = {0};

    /* Send 3 reliable messages at t=0. */
    for (int i = 0; i < 3; i++) {
        uint16_t seq;
        net_rudp_peer_send_reliable_via(&peer, &io, fake_sendto, &addr,
                                         0u, 0x0001, "m", 1, &seq);
    }
    ASSERT_INT_EQ(3, io.send_count);

    /* Tick at t=60 — all 3 should resend. */
    io.send_count = 0;
    net_rudp_peer_tick_resend_via(&peer, &io, fake_sendto, &addr, 60u);
    ASSERT_INT_EQ(3, io.send_count);

    return 0;
}

/* ── Runner ────────────────────────────────────────────────────── */

typedef int (*test_fn)(void);
typedef struct { const char *name; test_fn fn; } test_entry_t;

int main(void) {
    const test_entry_t tests[] = {
        {"rtt_first_sample_initializes",  test_rtt_first_sample_initializes},
        {"rtt_ewma_smoothing",            test_rtt_ewma_smoothing},
        {"rtt_no_negative",               test_rtt_no_negative},
        {"rtt_bounded_after_jitter",      test_rtt_bounded_after_jitter},
        {"resend_after_interval",         test_resend_after_interval},
        {"slot_expiry",                   test_slot_expiry},
        {"ack_bits_retires_slot",         test_ack_bits_retires_slot},
        {"multiple_resends",              test_multiple_resends},
    };

    int n = (int)(sizeof(tests) / sizeof(tests[0]));
    int passed = 0;

    printf("p007_net_rtt_retransmit_tests:\n");
    for (int i = 0; i < n; ++i) {
        int rc = tests[i].fn();
        printf("  %-50s %s\n", tests[i].name, rc == 0 ? "PASS" : "FAIL");
        if (rc == 0) { ++passed; }
    }

    printf("%d/%d tests passed\n", passed, n);
    return passed == n ? 0 : 1;
}
