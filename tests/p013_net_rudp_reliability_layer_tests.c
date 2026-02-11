#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/reliability.h"
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

#define ASSERT_MEM_EQ(exp, act, n)                                                                        \
    do {                                                                                                  \
        if (memcmp((exp), (act), (n)) != 0) {                                                              \
            TEST_FAIL("%s", "memcmp failed");                                                          \
        }                                                                                                 \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

#define NET_RUDP_FRAG_HDR_SIZE 8u

static void write_u16_be(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xFFu);
    out[1] = (uint8_t)(v & 0xFFu);
}

static int test_reliable_duplicate_dropped(void) {
    net_rudp_send_slot_t slots[1];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, ARRAY_SIZE(slots));

    const uint8_t payload[] = {0xAAu, 0xBBu};

    net_packet_header_t header;
    memset(&header, 0, sizeof(header));
    header.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    header.sequence = 10u;

    net_rudp_wire_frame_view_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.flags = NET_RUDP_WIRE_FLAG_RELIABLE;
    frame.schema_id = 0x1234u;
    frame.payload = payload;
    frame.payload_size = sizeof(payload);

    uint8_t out_reliable = 0u;
    uint16_t out_schema = 0u;
    uint8_t out_payload[16];
    size_t out_payload_size = 0u;

    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_receive(&peer, &header, &frame, 0u, &out_reliable, &out_schema, out_payload, sizeof(out_payload),
                                               &out_payload_size));
    ASSERT_TRUE(out_reliable != 0u);
    ASSERT_INT_EQ(0x1234u, out_schema);
    ASSERT_INT_EQ((int)sizeof(payload), (int)out_payload_size);
    ASSERT_MEM_EQ(payload, out_payload, sizeof(payload));

    /* Same reliable sequence again => duplicate => dropped. */
    ASSERT_INT_EQ(NET_RUDP_EMPTY,
                  net_rudp_reliability_receive(&peer, &header, &frame, 0u, &out_reliable, &out_schema, out_payload, sizeof(out_payload),
                                               &out_payload_size));
    return 0;
}

static int test_ack_retire_send_slots(void) {
    net_rudp_send_slot_t slots[4];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, ARRAY_SIZE(slots));

    peer.send_slots[0].used = 1u;
    peer.send_slots[0].sequence = 100u;
    peer.send_slots[1].used = 1u;
    peer.send_slots[1].sequence = 101u;

    net_packet_header_t header;
    memset(&header, 0, sizeof(header));
    header.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    header.ack = 101u;
    header.ack_bits[0] = 0x00000001ull; /* delta=1 => ack seq=100 */

    uint8_t dummy_payload_byte = 0u;
    net_rudp_wire_frame_view_t frame;
    memset(&frame, 0, sizeof(frame));
    frame.flags = 0u;
    frame.schema_id = 0x7777u;
    frame.payload = &dummy_payload_byte;
    frame.payload_size = 0u;

    uint8_t out_reliable = 0u;
    uint16_t out_schema = 0u;
    uint8_t out_payload[1];
    size_t out_payload_size = 0u;

    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_receive(&peer, &header, &frame, 0u, &out_reliable, &out_schema, out_payload, sizeof(out_payload),
                                               &out_payload_size));
    ASSERT_INT_EQ(0, (int)out_reliable);
    ASSERT_INT_EQ(0x7777u, out_schema);
    ASSERT_INT_EQ(0, (int)out_payload_size);

    ASSERT_INT_EQ(0, (int)peer.send_slots[0].used);
    ASSERT_INT_EQ(0, (int)peer.send_slots[1].used);
    return 0;
}

static int test_fragment_reassembly_yields_message(void) {
    net_rudp_send_slot_t slots[1];
    memset(slots, 0, sizeof(slots));

    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, ARRAY_SIZE(slots));

    uint8_t reasm_buf[2048];
    net_rudp_peer_enable_fragmentation(&peer, 1, reasm_buf, sizeof(reasm_buf));

    const size_t max_single = (NET_RUDP_MAX_PACKET_SIZE - NET_PACKET_HEADER_SIZE - NET_RUDP_WIRE_FRAME_HEADER_SIZE);
    ASSERT_TRUE(max_single > NET_RUDP_FRAG_HDR_SIZE);
    const size_t max_chunk = max_single - NET_RUDP_FRAG_HDR_SIZE;
    ASSERT_TRUE(max_chunk > 0u);

    const size_t total_size = max_chunk + 10u;
    ASSERT_TRUE(total_size <= sizeof(reasm_buf));

    uint8_t msg[2048];
    for (size_t i = 0u; i < total_size; ++i) {
        msg[i] = (uint8_t)(i & 0xFFu);
    }

    const uint16_t msg_id = 0x0042u;
    const uint16_t schema_id = 0xBEEFu;

    uint8_t frag0_payload[NET_RUDP_MAX_PACKET_SIZE];
    uint8_t frag1_payload[NET_RUDP_MAX_PACKET_SIZE];

    /* frag 0: max_chunk bytes */
    write_u16_be(frag0_payload + 0, msg_id);
    write_u16_be(frag0_payload + 2, 0u);
    write_u16_be(frag0_payload + 4, 2u);
    write_u16_be(frag0_payload + 6, (uint16_t)total_size);
    memcpy(frag0_payload + NET_RUDP_FRAG_HDR_SIZE, msg, max_chunk);
    const size_t frag0_size = NET_RUDP_FRAG_HDR_SIZE + max_chunk;

    /* frag 1: remaining bytes */
    write_u16_be(frag1_payload + 0, msg_id);
    write_u16_be(frag1_payload + 2, 1u);
    write_u16_be(frag1_payload + 4, 2u);
    write_u16_be(frag1_payload + 6, (uint16_t)total_size);
    memcpy(frag1_payload + NET_RUDP_FRAG_HDR_SIZE, msg + max_chunk, total_size - max_chunk);
    const size_t frag1_size = NET_RUDP_FRAG_HDR_SIZE + (total_size - max_chunk);

    uint8_t out_reliable = 0u;
    uint16_t out_schema = 0u;
    uint8_t out_payload[2048];
    size_t out_payload_size = 0u;

    net_packet_header_t header0;
    memset(&header0, 0, sizeof(header0));
    header0.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    header0.sequence = 1u;

    net_packet_header_t header1;
    memset(&header1, 0, sizeof(header1));
    header1.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    header1.sequence = 2u;

    net_rudp_wire_frame_view_t frame0;
    memset(&frame0, 0, sizeof(frame0));
    frame0.flags = (uint8_t)(NET_RUDP_WIRE_FLAG_RELIABLE | NET_RUDP_WIRE_FLAG_FRAGMENT);
    frame0.schema_id = schema_id;
    frame0.payload = frag0_payload;
    frame0.payload_size = frag0_size;

    net_rudp_wire_frame_view_t frame1;
    memset(&frame1, 0, sizeof(frame1));
    frame1.flags = (uint8_t)(NET_RUDP_WIRE_FLAG_RELIABLE | NET_RUDP_WIRE_FLAG_FRAGMENT);
    frame1.schema_id = schema_id;
    frame1.payload = frag1_payload;
    frame1.payload_size = frag1_size;

    ASSERT_INT_EQ(NET_RUDP_EMPTY,
                  net_rudp_reliability_receive(&peer, &header0, &frame0, 0u, &out_reliable, &out_schema, out_payload, sizeof(out_payload),
                                               &out_payload_size));

    ASSERT_INT_EQ(NET_RUDP_OK,
                  net_rudp_reliability_receive(&peer, &header1, &frame1, 0u, &out_reliable, &out_schema, out_payload, sizeof(out_payload),
                                               &out_payload_size));

    ASSERT_TRUE(out_reliable != 0u);
    ASSERT_INT_EQ((int)schema_id, (int)out_schema);
    ASSERT_INT_EQ((int)total_size, (int)out_payload_size);
    ASSERT_MEM_EQ(msg, out_payload, total_size);
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"reliable_duplicate_dropped", test_reliable_duplicate_dropped},
        {"ack_retire_send_slots", test_ack_retire_send_slots},
        {"fragment_reassembly_yields_message", test_fragment_reassembly_yields_message},
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
