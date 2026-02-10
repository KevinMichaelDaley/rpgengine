#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/event_batch.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/test_client.h"
#include "ferrum/net/test_transport.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/topic_channel.h"

#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/runtime.h"

#define TEST_FAIL(msg, ...)                                                                                 \
    do {                                                                                                     \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);                       \
        return 1;                                                                                            \
    } while (0)

#define ASSERT_TRUE(cond)                                                                                    \
    do {                                                                                                     \
        if (!(cond)) {                                                                                       \
            TEST_FAIL("%s", #cond);                                                                          \
        }                                                                                                    \
    } while (0)

#define ASSERT_EQ_INT(exp, act)                                                                              \
    do {                                                                                                     \
        long long _e = (long long)(exp);                                                                      \
        long long _a = (long long)(act);                                                                      \
        if (_e != _a) {                                                                                       \
            TEST_FAIL("expected %lld got %lld", _e, _a);                                                     \
        }                                                                                                    \
    } while (0)

#define ASSERT_EQ_U16(exp, act)                                                                              \
    do {                                                                                                     \
        uint16_t _e = (uint16_t)(exp);                                                                        \
        uint16_t _a = (uint16_t)(act);                                                                        \
        if (_e != _a) {                                                                                       \
            TEST_FAIL("expected %u got %u", (unsigned)_e, (unsigned)_a);                                     \
        }                                                                                                    \
    } while (0)

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

static bool get_out_topics_from_runtime_(void *user,
                                        uint16_t client_id,
                                        fr_topic_channel_t **out_rel,
                                        fr_topic_channel_t **out_unrel) {
    fr_server_net_runtime_t *rt = (fr_server_net_runtime_t *)user;
    if (!rt || !out_rel || !out_unrel) {
        return false;
    }
    return fr_server_net_runtime_client_out_topics(rt, client_id, out_rel, out_unrel);
}

static uint64_t transport_now_ms_(const fr_test_transport_t *t) {
    if (!t) {
        return 0u;
    }
    return net_test_clock_now_ns(&t->clock) / 1000000ull;
}

static void transport_advance_ms_(fr_test_transport_t *t, uint64_t delta_ms) {
    if (!t) {
        return;
    }
    net_test_clock_advance(&t->clock, delta_ms * 1000000ull);
}

static int drive_one_spin_(fr_test_transport_t *t,
                          fr_server_net_runtime_t *rt,
                          fr_server_entity_net_pump_t *pump,
                          fr_test_client_t **clients,
                          size_t client_count) {
    const uint64_t now_ms = transport_now_ms_(t);

    ASSERT_TRUE(fr_server_net_runtime_pump(rt, now_ms));
    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, now_ms));

    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    for (size_t i = 0u; i < client_count; ++i) {
        ASSERT_TRUE(fr_test_client_pump_rx(clients[i], now_ms));
    }

    return 0;
}

static int pop_reliable_schema_(fr_test_client_t *cl, uint16_t *out_schema, uint8_t *out_payload, size_t *inout_payload) {
    if (!cl || !out_schema || !out_payload || !inout_payload) {
        return 0;
    }

    uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
    size_t msg_len = sizeof(msg);
    if (!fr_test_client_pop_reliable(cl, 0u, msg, &msg_len)) {
        return 0;
    }
    if (msg_len < 2u) {
        return -1;
    }

    const uint16_t schema_id = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8u);
    const size_t payload_size = msg_len - 2u;
    if (*inout_payload < payload_size) {
        return -1;
    }
    if (payload_size > 0u) {
        memcpy(out_payload, msg + 2u, payload_size);
    }

    *out_schema = schema_id;
    *inout_payload = payload_size;
    return 1;
}

static int test_single_join_delivers_welcome_then_spawn_under_backpressure(void) {
    net_test_step_t steps[] = {{1u, 0u, 0u}};

    fr_test_transport_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.max_clients = 1u;
    tcfg.base_port = 42000u;
    tcfg.clock_start_ns = 0u;
    tcfg.link_slots = 64u;
    tcfg.max_payload_size = NET_RUDP_MAX_PACKET_SIZE;
    tcfg.client_to_server_steps = steps;
    tcfg.client_to_server_step_count = ARRAY_SIZE(steps);
    tcfg.server_to_client_steps = steps;
    tcfg.server_to_client_step_count = ARRAY_SIZE(steps);

    fr_test_transport_t *t = fr_test_transport_create(&tcfg);
    ASSERT_TRUE(t != NULL);

    fr_topic_channel_config_t topic_cfg = {.capacity = 64};
    fr_topic_channel_t *inbound = fr_topic_channel_create(&topic_cfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&topic_cfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&topic_cfg);
    ASSERT_TRUE(inbound && player_events && entity_events);

    fr_server_net_runtime_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.max_clients = 1u;
    rcfg.inbound_topic = inbound;
    rcfg.out_reliable_capacity = 1u; /* force WELCOME to occupy the only slot */
    rcfg.out_unreliable_capacity = 8u;
    rcfg.recvfrom_cb = fr_test_transport_recvfrom_cb;
    rcfg.sendto_cb = fr_test_transport_sendto_cb;
    rcfg.io_user = t;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&rcfg);
    ASSERT_TRUE(rt != NULL);

    fr_server_entity_net_pump_config_t pcfg;
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.max_clients = 1u;
    pcfg.tick_hz = 60u;
    pcfg.expected_entities = 1u;
    pcfg.inbound_topic = inbound;
    pcfg.player_event_topic = player_events;
    pcfg.entity_event_topic = entity_events;
    pcfg.get_client_out_topics_cb = get_out_topics_from_runtime_;
    pcfg.io_user = rt;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&pcfg);
    ASSERT_TRUE(pump != NULL);

    net_udp_addr_t server_addr;
    ASSERT_EQ_INT(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&server_addr, 127u, 0u, 0u, 1u, 1u));

    fr_test_client_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    ccfg.tx_link = &t->client_to_server_links[0];
    ccfg.rx_link = &t->server_to_client_links[0];
    ccfg.remote_addr = server_addr;
    ccfg.stream_channels = 1u;

    fr_test_client_t *cl = fr_test_client_create(&ccfg);
    ASSERT_TRUE(cl != NULL);

    net_repl_join_t join;
    join.client_nonce = 0x12345678u;
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_join_encode(&join, join_payload, sizeof(join_payload)));

    ASSERT_TRUE(fr_test_client_send_reliable(cl, transport_now_ms_(t), NET_REPL_SCHEMA_JOIN, join_payload, sizeof(join_payload)));

    fr_test_client_t *clients[] = {cl};

    /* First spin should deliver WELCOME, but spawn EVENT is backpressured. */
    ASSERT_EQ_INT(0, drive_one_spin_(t, rt, pump, clients, ARRAY_SIZE(clients)));

    uint16_t schema = 0u;
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
    size_t payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(cl, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_WELCOME, schema);

    net_repl_welcome_t w;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_welcome_decode(&w, payload, payload_len));
    ASSERT_EQ_INT(60u, w.tick_hz);
    ASSERT_EQ_INT(1u, w.expected_entities);

    /* No more reliable messages yet. */
    payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(cl, &schema, payload, &payload_len) == 0);

    /* Next tick should retry and deliver the spawn EVENT batch. */
    transport_advance_ms_(t, 1u);
    ASSERT_EQ_INT(0, drive_one_spin_(t, rt, pump, clients, ARRAY_SIZE(clients)));

    payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(cl, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_EVENT, schema);

    net_repl_event_entry_view_t entries[4] = {0};
    uint16_t entry_count = 0u;
    uint16_t server_tick = 0u;
    ASSERT_EQ_INT(NET_REPL_OK,
                  net_repl_event_batch_decode(&server_tick, entries, ARRAY_SIZE(entries), &entry_count, payload, payload_len));
    ASSERT_EQ_INT(1u, entry_count);
    ASSERT_EQ_INT(NET_REPL_EVENT_SPAWN, entries[0].type);

    net_repl_spawn_t sp;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_spawn_decode(&sp, entries[0].payload, entries[0].payload_size));
    ASSERT_EQ_INT(1000u, sp.entity_id);
    ASSERT_EQ_INT(0u, sp.owner_client_id);

    fr_test_client_destroy(cl);
    fr_server_entity_net_pump_destroy(pump);
    fr_server_net_runtime_destroy(rt);

    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    fr_test_transport_destroy(t);
    return 0;
}

static int test_second_join_delivers_spawn_batch_over_stream(void) {
    net_test_step_t steps[] = {{1u, 0u, 0u}};

    fr_test_transport_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.max_clients = 2u;
    tcfg.base_port = 43000u;
    tcfg.clock_start_ns = 0u;
    tcfg.link_slots = 128u;
    tcfg.max_payload_size = NET_RUDP_MAX_PACKET_SIZE;
    tcfg.client_to_server_steps = steps;
    tcfg.client_to_server_step_count = ARRAY_SIZE(steps);
    tcfg.server_to_client_steps = steps;
    tcfg.server_to_client_step_count = ARRAY_SIZE(steps);

    fr_test_transport_t *t = fr_test_transport_create(&tcfg);
    ASSERT_TRUE(t != NULL);

    fr_topic_channel_config_t topic_cfg = {.capacity = 128};
    fr_topic_channel_t *inbound = fr_topic_channel_create(&topic_cfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&topic_cfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&topic_cfg);
    ASSERT_TRUE(inbound && player_events && entity_events);

    fr_server_net_runtime_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.max_clients = 2u;
    rcfg.inbound_topic = inbound;
    rcfg.out_reliable_capacity = 8u;
    rcfg.out_unreliable_capacity = 8u;
    rcfg.recvfrom_cb = fr_test_transport_recvfrom_cb;
    rcfg.sendto_cb = fr_test_transport_sendto_cb;
    rcfg.io_user = t;

    fr_server_net_runtime_t *rt = fr_server_net_runtime_create(&rcfg);
    ASSERT_TRUE(rt != NULL);

    fr_server_entity_net_pump_config_t pcfg;
    memset(&pcfg, 0, sizeof(pcfg));
    pcfg.max_clients = 2u;
    pcfg.tick_hz = 60u;
    pcfg.expected_entities = 2u;
    pcfg.inbound_topic = inbound;
    pcfg.player_event_topic = player_events;
    pcfg.entity_event_topic = entity_events;
    pcfg.get_client_out_topics_cb = get_out_topics_from_runtime_;
    pcfg.io_user = rt;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&pcfg);
    ASSERT_TRUE(pump != NULL);

    net_udp_addr_t server_addr;
    ASSERT_EQ_INT(NET_UDP_SOCKET_OK, net_udp_addr_ipv4(&server_addr, 127u, 0u, 0u, 1u, 1u));

    fr_test_client_config_t ccfg;
    memset(&ccfg, 0, sizeof(ccfg));
    ccfg.protocol_id = NET_RUDP_PROTOCOL_ID_P008;
    ccfg.remote_addr = server_addr;
    ccfg.stream_channels = 1u;

    ccfg.tx_link = &t->client_to_server_links[0];
    ccfg.rx_link = &t->server_to_client_links[0];
    fr_test_client_t *c0 = fr_test_client_create(&ccfg);
    ASSERT_TRUE(c0 != NULL);

    ccfg.tx_link = &t->client_to_server_links[1];
    ccfg.rx_link = &t->server_to_client_links[1];
    fr_test_client_t *c1 = fr_test_client_create(&ccfg);
    ASSERT_TRUE(c1 != NULL);

    fr_test_client_t *clients[] = {c0, c1};

    /* Join client 0. */
    net_repl_join_t j0;
    j0.client_nonce = 0x11111111u;
    uint8_t j0_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_join_encode(&j0, j0_payload, sizeof(j0_payload)));
    ASSERT_TRUE(fr_test_client_send_reliable(c0, transport_now_ms_(t), NET_REPL_SCHEMA_JOIN, j0_payload, sizeof(j0_payload)));

    ASSERT_EQ_INT(0, drive_one_spin_(t, rt, pump, clients, ARRAY_SIZE(clients)));

    /* Drain WELCOME + EVENT from client0. */
    uint16_t schema = 0u;
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
    size_t payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(c0, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_WELCOME, schema);

    payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(c0, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_EVENT, schema);

    /* Join client 1. */
    net_repl_join_t j1;
    j1.client_nonce = 0x22222222u;
    uint8_t j1_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_join_encode(&j1, j1_payload, sizeof(j1_payload)));
    ASSERT_TRUE(fr_test_client_send_reliable(c1, transport_now_ms_(t), NET_REPL_SCHEMA_JOIN, j1_payload, sizeof(j1_payload)));

    transport_advance_ms_(t, 1u);
    ASSERT_EQ_INT(0, drive_one_spin_(t, rt, pump, clients, ARRAY_SIZE(clients)));

    /* Client1 should receive WELCOME then one EVENT batch containing both SPAWNs (self + client0). */
    payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(c1, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_WELCOME, schema);

    net_repl_welcome_t w;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_welcome_decode(&w, payload, payload_len));
    ASSERT_EQ_INT(2u, w.expected_entities);

    payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(c1, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_EVENT, schema);

    net_repl_event_entry_view_t entries[8] = {0};
    uint16_t count = 0u;
    uint16_t server_tick = 0u;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_event_batch_decode(&server_tick, entries, ARRAY_SIZE(entries), &count, payload, payload_len));
    ASSERT_EQ_INT(2u, count);

    net_repl_spawn_t sp_a;
    net_repl_spawn_t sp_b;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_spawn_decode(&sp_a, entries[0].payload, entries[0].payload_size));
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_spawn_decode(&sp_b, entries[1].payload, entries[1].payload_size));

    /* Order of SPAWNs inside the batch should be stable (by src player id). */
    ASSERT_EQ_INT(1000u, sp_a.entity_id);
    ASSERT_EQ_INT(0u, sp_a.owner_client_id);
    ASSERT_EQ_INT(1001u, sp_b.entity_id);
    ASSERT_EQ_INT(1u, sp_b.owner_client_id);

    /* Client0 should receive an EVENT batch spawning client1. */
    payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(c0, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_EVENT, schema);

    net_repl_event_entry_view_t e0[4] = {0};
    uint16_t e0_count = 0u;
    server_tick = 0u;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_event_batch_decode(&server_tick, e0, ARRAY_SIZE(e0), &e0_count, payload, payload_len));
    ASSERT_EQ_INT(1u, e0_count);

    net_repl_spawn_t sp_new;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_spawn_decode(&sp_new, e0[0].payload, e0[0].payload_size));
    ASSERT_EQ_INT(1001u, sp_new.entity_id);
    ASSERT_EQ_INT(1u, sp_new.owner_client_id);

    fr_test_client_destroy(c0);
    fr_test_client_destroy(c1);
    fr_server_entity_net_pump_destroy(pump);
    fr_server_net_runtime_destroy(rt);

    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    fr_test_transport_destroy(t);
    return 0;
}

struct test_case {
    const char *name;
    int (*fn)(void);
};

static struct test_case TESTS[] = {
    {"single_join_delivers_welcome_then_spawn_under_backpressure", test_single_join_delivers_welcome_then_spawn_under_backpressure},
    {"second_join_delivers_spawn_batch_over_stream", test_second_join_delivers_spawn_batch_over_stream},
};

int main(void) {
    size_t passed = 0u;
    for (size_t i = 0u; i < ARRAY_SIZE(TESTS); ++i) {
        printf("RUN %s\n", TESTS[i].name);
        int rc = TESTS[i].fn();
        if (rc != 0) {
            printf("FAIL %s\n", TESTS[i].name);
            return 1;
        }
        printf("PASS %s\n", TESTS[i].name);
        passed++;
    }
    printf("%zu/%zu tests passed\n", passed, ARRAY_SIZE(TESTS));
    return 0;
}
