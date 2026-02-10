#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/event_batch.h"
#include "ferrum/net/replication/input_rot.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/test_client.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/test_transport.h"
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

static int drive_one_spin_(fr_test_transport_t *t,
                          fr_server_net_runtime_t *rt,
                          fr_server_entity_net_pump_t *pump,
                          fr_test_client_t **clients,
                          size_t client_count) {
    const uint64_t now_ms = transport_now_ms_(t);

    for (size_t i = 0u; i < client_count; ++i) {
        ASSERT_TRUE(fr_test_client_tick_resend(clients[i], now_ms));
    }

    ASSERT_TRUE(fr_server_net_runtime_pump(rt, now_ms));
    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, now_ms));

    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    for (size_t i = 0u; i < client_count; ++i) {
        ASSERT_TRUE(fr_test_client_pump_rx(clients[i], now_ms));
    }

    return 0;
}

static int send_ack_ping_(fr_test_client_t *cl, uint64_t now_ms, uint32_t event_id) {
    net_repl_input_rot_t ping;
    memset(&ping, 0, sizeof(ping));
    ping.entity_id = 0u; /* allow pump to remap to (1000 + client_id) */
    ping.event_id = event_id;

    uint8_t payload[NET_REPL_INPUT_ROT_PAYLOAD_SIZE];
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_input_rot_encode(&ping, payload, sizeof(payload)));

    ASSERT_TRUE(fr_test_client_send_unreliable(cl, now_ms, NET_REPL_SCHEMA_INPUT_ROT, payload, sizeof(payload)));
    return 0;
}

static int test_lossy_join_still_delivers_welcome_then_spawn_event(void) {
    /* Client->server: drop the first JOIN send (forces client resend). */
    net_test_step_t up_steps[] = {{0u, 0u, 0u}, {1u, 0u, 0u}};

    /* Server->client: drop WELCOME frame, drop EVENT frame, drop first WELCOME resend.
       Allow EVENT resend to arrive before WELCOME to exercise stream ordering.
     */
    net_test_step_t down_steps[] = {{0u, 0u, 0u}, {0u, 0u, 0u}, {0u, 0u, 0u}, {1u, 0u, 0u}, {1u, 0u, 0u}};

    fr_test_transport_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.max_clients = 1u;
    tcfg.base_port = 44000u;
    tcfg.clock_start_ns = 0u;
    tcfg.link_slots = 256u;
    tcfg.max_payload_size = NET_RUDP_MAX_PACKET_SIZE;
    tcfg.client_to_server_steps = up_steps;
    tcfg.client_to_server_step_count = ARRAY_SIZE(up_steps);
    tcfg.server_to_client_steps = down_steps;
    tcfg.server_to_client_step_count = ARRAY_SIZE(down_steps);

    fr_test_transport_t *t = fr_test_transport_create(&tcfg);
    ASSERT_TRUE(t != NULL);

    fr_topic_channel_config_t topic_cfg = {.capacity = 256};
    fr_topic_channel_t *inbound = fr_topic_channel_create(&topic_cfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&topic_cfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&topic_cfg);
    ASSERT_TRUE(inbound && player_events && entity_events);

    fr_server_net_runtime_config_t rcfg;
    memset(&rcfg, 0, sizeof(rcfg));
    rcfg.max_clients = 1u;
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
    ccfg.resend_interval_ms = 20u;
    ccfg.tx_link = &t->client_to_server_links[0];
    ccfg.rx_link = &t->server_to_client_links[0];
    ccfg.remote_addr = server_addr;
    ccfg.stream_channels = 1u;

    fr_test_client_t *cl = fr_test_client_create(&ccfg);
    ASSERT_TRUE(cl != NULL);

    net_repl_join_t join;
    join.client_nonce = 0xAABBCCDDu;
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_join_encode(&join, join_payload, sizeof(join_payload)));

    ASSERT_TRUE(fr_test_client_send_reliable(cl, transport_now_ms_(t), NET_REPL_SCHEMA_JOIN, join_payload, sizeof(join_payload)));

    fr_test_client_t *clients[] = {cl};

    int saw_welcome = 0;
    int saw_event = 0;

    uint32_t ack_event_id = 1u;

    for (int iter = 0; iter < 64; ++iter) {
        ASSERT_EQ_INT(0, drive_one_spin_(t, rt, pump, clients, ARRAY_SIZE(clients)));

        /* Send an ack-carrying packet back to the server so it can retire send slots. */
        ASSERT_EQ_INT(0, send_ack_ping_(cl, transport_now_ms_(t), ack_event_id++));

        for (;;) {
            uint16_t schema = 0u;
            uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
            size_t payload_len = sizeof(payload);
            int prc = pop_reliable_schema_(cl, &schema, payload, &payload_len);
            if (prc == 0) {
                break;
            }
            ASSERT_EQ_INT(1, prc);

            if (!saw_welcome) {
                ASSERT_EQ_U16(NET_REPL_SCHEMA_WELCOME, schema);
                net_repl_welcome_t w;
                ASSERT_EQ_INT(NET_REPL_OK, net_repl_welcome_decode(&w, payload, payload_len));
                ASSERT_EQ_INT(60u, w.tick_hz);
                ASSERT_EQ_INT(1u, w.expected_entities);
                saw_welcome = 1;
                continue;
            }

            if (!saw_event) {
                ASSERT_EQ_U16(NET_REPL_SCHEMA_EVENT, schema);

                net_repl_event_entry_view_t entries[4] = {0};
                uint16_t count = 0u;
                uint16_t server_tick = 0u;
                ASSERT_EQ_INT(NET_REPL_OK,
                              net_repl_event_batch_decode(&server_tick, entries, ARRAY_SIZE(entries), &count, payload, payload_len));
                ASSERT_EQ_INT(1u, count);
                ASSERT_EQ_INT(NET_REPL_EVENT_SPAWN, entries[0].type);

                net_repl_spawn_t sp;
                ASSERT_EQ_INT(NET_REPL_OK, net_repl_spawn_decode(&sp, entries[0].payload, entries[0].payload_size));
                ASSERT_EQ_INT(1000u, sp.entity_id);
                ASSERT_EQ_INT(0u, sp.owner_client_id);

                saw_event = 1;
                continue;
            }

            TEST_FAIL("unexpected extra reliable message schema=0x%04x", (unsigned)schema);
        }

        if (saw_welcome && saw_event) {
            break;
        }

        /* Ensure server resend interval is exceeded. */
        transport_advance_ms_(t, 60u);
    }

    ASSERT_TRUE(saw_welcome);
    ASSERT_TRUE(saw_event);

    fr_test_client_destroy(cl);
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
    {"lossy_join_still_delivers_welcome_then_spawn_event", test_lossy_join_still_delivers_welcome_then_spawn_event},
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
