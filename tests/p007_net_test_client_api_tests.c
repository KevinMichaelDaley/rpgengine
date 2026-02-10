#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/test_client.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_link.h"

#define ASSERT_TRUE(cond)                                                                                \
    do {                                                                                                 \
        if (!(cond)) {                                                                                   \
            fprintf(stderr, "ASSERT_TRUE failed: %s:%d: %s\n", __FILE__, __LINE__, #cond);              \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_EQ_U16(exp, act)                                                                          \
    do {                                                                                                 \
        if ((uint16_t)(exp) != (uint16_t)(act)) {                                                        \
            fprintf(stderr, "ASSERT_EQ_U16 failed: %s:%d: expected %u got %u\n", __FILE__, __LINE__,    \
                    (unsigned)(uint16_t)(exp), (unsigned)(uint16_t)(act));                               \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ASSERT_EQ_MEM(exp, act, n)                                                                       \
    do {                                                                                                 \
        if (memcmp((exp), (act), (n)) != 0) {                                                             \
            fprintf(stderr, "ASSERT_EQ_MEM failed: %s:%d\n", __FILE__, __LINE__);                        \
            return 1;                                                                                    \
        }                                                                                                \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static int sendto_link_(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    (void)to;
    net_test_link_t *link = (net_test_link_t *)io_user;
    return (net_test_link_send(link, data, size) == NET_TEST_LINK_OK) ? 0 : -1;
}

static int test_reliable_stream_frame_is_reassembled(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {{1u, 0u, 0u}};

    net_test_link_t down;
    ASSERT_TRUE(net_test_link_init(&down, &clock, steps, ARRAY_SIZE(steps), 8u, NET_RUDP_MAX_PACKET_SIZE) == NET_TEST_LINK_OK);

    net_test_link_t up;
    ASSERT_TRUE(net_test_link_init(&up, &clock, steps, ARRAY_SIZE(steps), 8u, NET_RUDP_MAX_PACKET_SIZE) == NET_TEST_LINK_OK);

    net_udp_addr_t dummy;
    ASSERT_TRUE(net_udp_addr_ipv4(&dummy, 127, 0, 0, 1, 40000) == NET_UDP_SOCKET_OK);

    fr_test_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    cfg.tx_link = &up;
    cfg.rx_link = &down;
    cfg.remote_addr = dummy;
    cfg.stream_channels = 1u;

    fr_test_client_t *cl = fr_test_client_create(&cfg);
    ASSERT_TRUE(cl != NULL);

    /* Simulate server sending one STREAM_FRAME as a reliable RUDP message. */
    net_rudp_peer_t server_peer;
    net_rudp_send_slot_t server_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(server_slots, 0, sizeof(server_slots));
    net_rudp_peer_init_with_storage(&server_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, server_slots, ARRAY_SIZE(server_slots));

    const uint8_t msg[] = {0x34u, 0x12u, 'h', 'i'}; /* pretend [schema_id:u16 LE][payload] */
    uint8_t stream_frame[4u + sizeof(msg)];
    /* [seq:u16 LE][chan:u16 LE] */
    stream_frame[0] = 1u;
    stream_frame[1] = 0u;
    stream_frame[2] = 0u;
    stream_frame[3] = 0u;
    memcpy(stream_frame + 4u, msg, sizeof(msg));

    uint16_t seq = 0u;
    ASSERT_TRUE(net_rudp_peer_send_reliable_via(&server_peer,
                                                &down,
                                                sendto_link_,
                                                &dummy,
                                                0u,
                                                NET_REPL_SCHEMA_STREAM_FRAME,
                                                stream_frame,
                                                sizeof(stream_frame),
                                                &seq) == NET_RUDP_OK);

    ASSERT_TRUE(fr_test_client_pump_rx(cl, 0u));

    uint8_t out[32];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(fr_test_client_pop_reliable(cl, 0u, out, &out_len));
    ASSERT_TRUE(out_len == sizeof(msg));
    ASSERT_EQ_MEM(msg, out, sizeof(msg));

    fr_test_client_destroy(cl);
    net_test_link_destroy(&up);
    net_test_link_destroy(&down);
    return 0;
}

static int test_unreliable_schema_is_queued(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {{1u, 0u, 0u}};

    net_test_link_t down;
    ASSERT_TRUE(net_test_link_init(&down, &clock, steps, ARRAY_SIZE(steps), 8u, NET_RUDP_MAX_PACKET_SIZE) == NET_TEST_LINK_OK);

    net_test_link_t up;
    ASSERT_TRUE(net_test_link_init(&up, &clock, steps, ARRAY_SIZE(steps), 8u, NET_RUDP_MAX_PACKET_SIZE) == NET_TEST_LINK_OK);

    net_udp_addr_t dummy;
    ASSERT_TRUE(net_udp_addr_ipv4(&dummy, 127, 0, 0, 1, 40000) == NET_UDP_SOCKET_OK);

    fr_test_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    cfg.tx_link = &up;
    cfg.rx_link = &down;
    cfg.remote_addr = dummy;

    fr_test_client_t *cl = fr_test_client_create(&cfg);
    ASSERT_TRUE(cl != NULL);

    net_rudp_peer_t server_peer;
    net_rudp_send_slot_t server_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(server_slots, 0, sizeof(server_slots));
    net_rudp_peer_init_with_storage(&server_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, server_slots, ARRAY_SIZE(server_slots));

    const uint8_t payload[] = {1u, 2u, 3u};
    ASSERT_TRUE(net_rudp_peer_send_unreliable_via(&server_peer,
                                                  &down,
                                                  sendto_link_,
                                                  &dummy,
                                                  0u,
                                                  NET_REPL_SCHEMA_STATE_CUBE,
                                                  payload,
                                                  sizeof(payload)) == NET_RUDP_OK);

    ASSERT_TRUE(fr_test_client_pump_rx(cl, 0u));

    uint16_t got_schema = 0u;
    uint8_t out[16];
    size_t out_len = sizeof(out);
    ASSERT_TRUE(fr_test_client_pop_unreliable(cl, &got_schema, out, &out_len));
    ASSERT_EQ_U16(NET_REPL_SCHEMA_STATE_CUBE, got_schema);
    ASSERT_TRUE(out_len == sizeof(payload));
    ASSERT_EQ_MEM(payload, out, sizeof(payload));

    fr_test_client_destroy(cl);
    net_test_link_destroy(&up);
    net_test_link_destroy(&down);
    return 0;
}

static int test_client_send_reliable_emits_rudp_packet(void) {
    net_test_clock_t clock;
    net_test_clock_init(&clock, 0u);

    net_test_step_t steps[] = {{1u, 0u, 0u}};

    net_test_link_t up;
    ASSERT_TRUE(net_test_link_init(&up, &clock, steps, ARRAY_SIZE(steps), 8u, NET_RUDP_MAX_PACKET_SIZE) == NET_TEST_LINK_OK);

    net_test_link_t down;
    ASSERT_TRUE(net_test_link_init(&down, &clock, steps, ARRAY_SIZE(steps), 8u, NET_RUDP_MAX_PACKET_SIZE) == NET_TEST_LINK_OK);

    net_udp_addr_t dummy;
    ASSERT_TRUE(net_udp_addr_ipv4(&dummy, 127, 0, 0, 1, 40000) == NET_UDP_SOCKET_OK);

    fr_test_client_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    cfg.tx_link = &up;
    cfg.rx_link = &down;
    cfg.remote_addr = dummy;

    fr_test_client_t *cl = fr_test_client_create(&cfg);
    ASSERT_TRUE(cl != NULL);

    const uint8_t payload[] = {9u, 8u, 7u, 6u};
    ASSERT_TRUE(fr_test_client_send_reliable(cl, 0u, NET_REPL_SCHEMA_JOIN, payload, sizeof(payload)));

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    size_t packet_size = 0u;
    ASSERT_TRUE(net_test_link_receive(&up, packet, sizeof(packet), &packet_size) == NET_TEST_LINK_OK);

    net_rudp_peer_t server_peer;
    net_rudp_send_slot_t server_slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(server_slots, 0, sizeof(server_slots));
    net_rudp_peer_init_with_storage(&server_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, server_slots, ARRAY_SIZE(server_slots));

    uint8_t reliable = 0u;
    uint16_t schema_id = 0u;
    uint8_t out_payload[NET_RUDP_MAX_PACKET_SIZE];
    size_t out_payload_size = 0u;
    ASSERT_TRUE(net_rudp_peer_receive(&server_peer,
                                      packet,
                                      packet_size,
                                      0u,
                                      &reliable,
                                      &schema_id,
                                      out_payload,
                                      sizeof(out_payload),
                                      &out_payload_size) == NET_RUDP_OK);

    ASSERT_TRUE(reliable != 0u);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_JOIN, schema_id);
    ASSERT_TRUE(out_payload_size == sizeof(payload));
    ASSERT_EQ_MEM(payload, out_payload, sizeof(payload));

    fr_test_client_destroy(cl);
    net_test_link_destroy(&up);
    net_test_link_destroy(&down);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"reliable_stream_frame_is_reassembled", test_reliable_stream_frame_is_reassembled},
    {"unreliable_schema_is_queued", test_unreliable_schema_is_queued},
    {"client_send_reliable_emits_rudp_packet", test_client_send_reliable_emits_rudp_packet},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;

    for (size_t i = 0u; i < total; ++i) {
        printf("RUN %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc != 0) {
            fprintf(stderr, "FAIL %s (rc=%d)\n", TESTS[i].name, rc);
            return 1;
        }
        printf("OK %s\n", TESTS[i].name);
        passed++;
    }

    printf("All %zu tests passed\n", passed);
    return 0;
}
