#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/replication/common.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/wire_frame.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/server/net/runtime.h"

#define TEST_FAIL(msg, ...)                                                                         \
    do {                                                                                            \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);               \
        return 1;                                                                                   \
    } while (0)

#define ASSERT_TRUE(cond)                                                                           \
    do {                                                                                            \
        if (!(cond)) {                                                                              \
            TEST_FAIL("%s", #cond);                                                                \
        }                                                                                           \
    } while (0)

#define ASSERT_EQ_INT(expected, actual)                                                             \
    do {                                                                                            \
        long long _exp = (long long)(expected);                                                     \
        long long _act = (long long)(actual);                                                       \
        if (_exp != _act) {                                                                         \
            TEST_FAIL("expected %lld got %lld", _exp, _act);                                       \
        }                                                                                           \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static uint64_t now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)ts.tv_nsec / 1000000ull;
}

struct test_net_in {
    net_udp_addr_t from;
    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    size_t size;
    int used;
};

struct test_net_out {
    net_udp_addr_t to;
    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    size_t size;
    int used;
};

struct test_net_io {
    struct test_net_in in;
    struct test_net_out out;
};

static int test_recvfrom(void *user,
                         net_udp_addr_t *out_from,
                         uint8_t *out_data,
                         size_t out_cap,
                         size_t *out_size) {
    struct test_net_io *io = (struct test_net_io *)user;
    if (!io || !out_from || !out_data || !out_size) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    if (!io->in.used) {
        return NET_UDP_SOCKET_EMPTY;
    }
    if (io->in.size > out_cap) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    *out_from = io->in.from;
    memcpy(out_data, io->in.packet, io->in.size);
    *out_size = io->in.size;
    io->in.used = 0;
    return NET_UDP_SOCKET_OK;
}

static int test_sendto(void *user, const net_udp_addr_t *to, const void *data, size_t size) {
    struct test_net_io *io = (struct test_net_io *)user;
    if (!io || !to || !data) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    if (size > sizeof(io->out.packet)) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    io->out.to = *to;
    memcpy(io->out.packet, data, size);
    io->out.size = size;
    io->out.used = 1;
    return NET_UDP_SOCKET_OK;
}

static void write_u16_be(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xFFu);
    out[1] = (uint8_t)(v & 0xFFu);
}

static int build_join_packet(net_rudp_peer_t *client_peer,
                             uint32_t nonce,
                             uint8_t *out_packet,
                             size_t out_cap,
                             size_t *out_size) {
    net_repl_join_t join;
    join.client_nonce = nonce;
    uint8_t payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(0, net_repl_join_encode(&join, payload, sizeof(payload)));

    /* Build one RUDP-framed packet matching net_rudp_peer_send_* wire layout,
       without performing any socket IO.
     */
    const size_t frame_size = 8u;
    const size_t packet_size = NET_PACKET_HEADER_SIZE + frame_size + sizeof(payload);
    ASSERT_TRUE(out_packet != NULL);
    ASSERT_TRUE(out_size != NULL);
    ASSERT_TRUE(out_cap >= packet_size);

    net_packet_header_t header;
    header.protocol_id = client_peer->protocol_id;
    header.sequence = client_peer->next_sequence;
    header.ack = net_ack_window_ack(&client_peer->recv_window);
    header.ack_bits = net_ack_window_ack_bits(&client_peer->recv_window);
    ASSERT_EQ_INT(0, net_packet_header_encode(&header, out_packet, out_cap));

    uint8_t *frame = out_packet + NET_PACKET_HEADER_SIZE;
    frame[0] = 0x01u; /* reliable */
    frame[1] = 0u;
    write_u16_be(frame + 2, NET_REPL_SCHEMA_JOIN);
    write_u16_be(frame + 4, (uint16_t)sizeof(payload));
    write_u16_be(frame + 6, 0u);
    memcpy(frame + frame_size, payload, sizeof(payload));

    *out_size = packet_size;
    client_peer->next_sequence = (uint16_t)(client_peer->next_sequence + 1u);
    return 0;
}

static int test_inbound_join_is_published_to_global_topic(void) {
    fr_topic_channel_config_t tcfg = {.capacity = 64};
    fr_topic_channel_t *inbox = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(inbox != NULL);

    struct test_net_io io;
    memset(&io, 0, sizeof(io));

    net_udp_addr_t client_addr;
    ASSERT_EQ_INT(0, net_udp_addr_ipv4(&client_addr, 10, 0, 0, 2, 40011));

    net_udp_addr_t server_addr;
    ASSERT_EQ_INT(0, net_udp_addr_ipv4(&server_addr, 10, 0, 0, 1, 40002));

    /* Build a single JOIN packet as if from a client. */
    net_rudp_peer_t client_peer;
    net_rudp_send_slot_t slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(slots, 0, sizeof(slots));
    net_rudp_peer_init_with_storage(&client_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, NET_RUDP_SEND_SLOTS_DEFAULT);

    io.in.from = client_addr;
    io.in.used = 1;
    (void)server_addr;
    ASSERT_EQ_INT(0, build_join_packet(&client_peer, 0x12345678u, io.in.packet, sizeof(io.in.packet), &io.in.size));

    fr_server_net_runtime_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 4;
    cfg.inbound_topic = inbox;
    cfg.recvfrom_cb = test_recvfrom;
    cfg.sendto_cb = test_sendto;
    cfg.io_user = &io;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&cfg);
    ASSERT_TRUE(rt != NULL);

    /* Pump once to dispatch the inbound datagram; then wait for fibers to run.
       In deterministic mode, wait_idle runs the fiber work.
     */
    ASSERT_TRUE(fr_server_net_runtime_pump(rt, now_ms()));
    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    /* Expect a single inbound message tagged with client_id and schema JOIN. */
    uint8_t msg[256];
    size_t msg_len = sizeof(msg);
    ASSERT_TRUE(fr_topic_channel_pop(inbox, msg, &msg_len));
    ASSERT_TRUE(msg_len >= 6);

    uint16_t client_id = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8u);
    uint16_t schema_id = (uint16_t)msg[2] | ((uint16_t)msg[3] << 8u);
    uint8_t flags = msg[4];
    (void)flags;

    ASSERT_TRUE(client_id < 4);
    ASSERT_EQ_INT(NET_REPL_SCHEMA_JOIN, schema_id);

    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(inbox);
    return 0;
}

static int test_outbound_reliable_topic_sends_a_packet(void) {
    fr_topic_channel_config_t tcfg = {.capacity = 64};
    fr_topic_channel_t *inbox = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(inbox != NULL);

    struct test_net_io io;
    memset(&io, 0, sizeof(io));

    fr_server_net_runtime_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 1;
    cfg.inbound_topic = inbox;
    cfg.recvfrom_cb = test_recvfrom;
    cfg.sendto_cb = test_sendto;
    cfg.io_user = &io;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&cfg);
    ASSERT_TRUE(rt != NULL);

        /* Force-create client 0 by injecting a JOIN packet from an address.
             The runtime allocates clients on first JOIN for a new transport endpoint.
         */
    net_udp_addr_t client_addr;
    ASSERT_EQ_INT(0, net_udp_addr_ipv4(&client_addr, 10, 0, 0, 2, 40011));

        net_rudp_peer_t client_peer;
        net_rudp_send_slot_t slots[NET_RUDP_SEND_SLOTS_DEFAULT];
        memset(slots, 0, sizeof(slots));
        net_rudp_peer_init_with_storage(&client_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, NET_RUDP_SEND_SLOTS_DEFAULT);

        io.in.from = client_addr;
        io.in.used = 1;
        ASSERT_EQ_INT(0, build_join_packet(&client_peer, 0x11112222u, io.in.packet, sizeof(io.in.packet), &io.in.size));

    (void)fr_server_net_runtime_pump(rt, now_ms());
    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    fr_topic_channel_t *out_rel = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    ASSERT_TRUE(fr_server_net_runtime_client_out_topics(rt, 0, &out_rel, &out_unrel));
    ASSERT_TRUE(out_rel != NULL);
    ASSERT_TRUE(out_unrel != NULL);

    /* Enqueue a WELCOME payload onto the reliable outbound topic; fiber should send it. */
    net_repl_welcome_t w;
    w.expected_entities = 1;
    w.tick_hz = 60;
    uint8_t payload[NET_REPL_WELCOME_PAYLOAD_SIZE];
    ASSERT_EQ_INT(0, net_repl_welcome_encode(&w, payload, sizeof(payload)));

    /* Topic message format: [schema_id:u16 LE][payload...] */
    uint8_t msg[2 + NET_REPL_WELCOME_PAYLOAD_SIZE];
    msg[0] = (uint8_t)(NET_REPL_SCHEMA_WELCOME & 0xFFu);
    msg[1] = (uint8_t)((NET_REPL_SCHEMA_WELCOME >> 8u) & 0xFFu);
    memcpy(msg + 2, payload, sizeof(payload));

    ASSERT_TRUE(fr_topic_channel_push(out_rel, msg, sizeof(msg)));

    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    ASSERT_TRUE(io.out.used);
    ASSERT_TRUE(io.out.size > 0);

    net_packet_header_t out_header;
    net_rudp_wire_frame_view_t out_frame;
    ASSERT_EQ_INT(NET_RUDP_WIRE_OK, net_rudp_wire_decode(&out_header, &out_frame, io.out.packet, io.out.size));
    (void)out_header;

    ASSERT_EQ_INT(NET_REPL_SCHEMA_STREAM_FRAME, out_frame.schema_id);
    ASSERT_TRUE((out_frame.flags & NET_RUDP_WIRE_FLAG_RELIABLE) != 0u);

    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(inbox);
    return 0;
}

static int test_outbound_topic_capacity_is_configurable(void) {
    fr_topic_channel_config_t tcfg = {.capacity = 64};
    fr_topic_channel_t *inbox = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(inbox != NULL);

    struct test_net_io io;
    memset(&io, 0, sizeof(io));

    fr_server_net_runtime_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 1;
    cfg.inbound_topic = inbox;
    cfg.recvfrom_cb = test_recvfrom;
    cfg.sendto_cb = test_sendto;
    cfg.io_user = &io;

    /* Configure small capacities so the test stays fast and deterministic. */
    cfg.out_reliable_capacity = 8u;
    cfg.out_unreliable_capacity = 9u;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&cfg);
    ASSERT_TRUE(rt != NULL);

    /* Allocate client 0 by sending a JOIN packet from some address. */
    net_udp_addr_t client_addr;
    ASSERT_EQ_INT(0, net_udp_addr_ipv4(&client_addr, 10, 0, 0, 2, 40011));

    net_rudp_peer_t client_peer;
    net_rudp_send_slot_t slots[NET_RUDP_SEND_SLOTS_DEFAULT];
    memset(slots, 0, sizeof(slots));
    net_rudp_peer_init_with_storage(&client_peer, NET_RUDP_PROTOCOL_ID_P008, 50u, slots, NET_RUDP_SEND_SLOTS_DEFAULT);

    io.in.from = client_addr;
    io.in.used = 1;
    ASSERT_EQ_INT(0, build_join_packet(&client_peer, 0xABCDEF01u, io.in.packet, sizeof(io.in.packet), &io.in.size));

    ASSERT_TRUE(fr_server_net_runtime_pump(rt, now_ms()));
    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    fr_topic_channel_t *out_rel = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    ASSERT_TRUE(fr_server_net_runtime_client_out_topics(rt, 0, &out_rel, &out_unrel));
    ASSERT_TRUE(out_rel != NULL);
    ASSERT_TRUE(out_unrel != NULL);

    /* Push exactly capacity messages; last push should fail. */
    uint8_t msg[3] = {0x01u, 0x02u, 0x03u};

    for (uint32_t i = 0u; i < cfg.out_reliable_capacity; ++i) {
        ASSERT_TRUE(fr_topic_channel_push(out_rel, msg, sizeof(msg)));
    }
    ASSERT_TRUE(!fr_topic_channel_push(out_rel, msg, sizeof(msg)));

    for (uint32_t i = 0u; i < cfg.out_unreliable_capacity; ++i) {
        ASSERT_TRUE(fr_topic_channel_push(out_unrel, msg, sizeof(msg)));
    }
    ASSERT_TRUE(!fr_topic_channel_push(out_unrel, msg, sizeof(msg)));

    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(inbox);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"inbound_join_is_published_to_global_topic", test_inbound_join_is_published_to_global_topic},
    {"outbound_reliable_topic_sends_a_packet", test_outbound_reliable_topic_sends_a_packet},
    {"outbound_topic_capacity_is_configurable", test_outbound_topic_capacity_is_configurable},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0;
    for (size_t i = 0; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        fflush(stdout);
        int rc = tc->fn();
        if (rc == 0) {
            passed++;
            printf("OK %s\n", tc->name);
        } else {
            fprintf(stderr, "Test failed: %s (rc=%d)\n", tc->name, rc);
            break;
        }
    }

    if (passed == total) {
        printf("All %zu tests passed\n", passed);
        return 0;
    }
    return 1;
}
