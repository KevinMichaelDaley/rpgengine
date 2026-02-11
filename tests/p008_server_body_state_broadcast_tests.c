#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ferrum/net/replication/body_state.h"
#include "ferrum/net/replication/body_state_batch.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/test_client.h"
#include "ferrum/net/test_transport.h"
#include "ferrum/net/test_clock.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/physics/world.h"

#include "ferrum/server/entity/net/pump.h"
#include "ferrum/server/net/runtime.h"
#include "ferrum/server/physics/net/body_state_broadcast.h"

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
    return net_test_clock_now_ns(&t->clock) / 1000000ull;
}

static void transport_advance_ms_(fr_test_transport_t *t, uint64_t delta_ms) {
    net_test_clock_advance(&t->clock, delta_ms * 1000000ull);
}

static int drive_one_tick_(fr_test_transport_t *t,
                           fr_server_net_runtime_t *rt,
                           fr_server_entity_net_pump_t *pump,
                           fr_server_body_state_broadcast_t *bcast,
                           uint16_t server_tick,
                           fr_test_client_t *cl) {
    const uint64_t now_ms = transport_now_ms_(t);

    ASSERT_TRUE(fr_server_net_runtime_pump(rt, now_ms));
    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));

    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, now_ms));
    ASSERT_TRUE(fr_server_body_state_broadcast_tick(bcast, server_tick, now_ms));

    ASSERT_TRUE(fr_server_net_runtime_run_fibers(rt, 1000u));
    ASSERT_TRUE(fr_test_client_pump_rx(cl, now_ms));

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

static int setup_connected_client_(fr_test_transport_t *t,
                                  fr_server_net_runtime_t *rt,
                                  fr_server_entity_net_pump_t *pump,
                                  fr_server_body_state_broadcast_t *bcast,
                                  fr_test_client_t *cl) {
    net_repl_join_t join;
    join.client_nonce = 0x12345678u;
    uint8_t join_payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_join_encode(&join, join_payload, sizeof(join_payload)));

    ASSERT_TRUE(fr_test_client_send_reliable(cl,
                                             transport_now_ms_(t),
                                             NET_REPL_SCHEMA_JOIN,
                                             join_payload,
                                             sizeof(join_payload)));

    ASSERT_EQ_INT(0, drive_one_tick_(t, rt, pump, bcast, 0u, cl));

    uint16_t schema = 0u;
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
    size_t payload_len = sizeof(payload);
    ASSERT_TRUE(pop_reliable_schema_(cl, &schema, payload, &payload_len) == 1);
    ASSERT_EQ_U16(NET_REPL_SCHEMA_WELCOME, schema);

    net_repl_welcome_t w;
    ASSERT_EQ_INT(NET_REPL_OK, net_repl_welcome_decode(&w, payload, payload_len));

    /* Connection tick may also deliver queued unreliable state; drain it so
       subsequent tests can assert exact tick counts. */
    uint16_t uschema = 0u;
    uint8_t upayload[NET_RUDP_MAX_PACKET_SIZE];
    size_t upayload_len = sizeof(upayload);
    while (fr_test_client_pop_unreliable(cl, &uschema, upayload, &upayload_len)) {
        upayload_len = sizeof(upayload);
    }

    return 0;
}

static int test_body_state_broadcast_sends_and_quantizes(void) {
    net_test_step_t steps[] = {{1u, 0u, 0u}};

    fr_test_transport_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.max_clients = 1u;
    tcfg.base_port = 42010u;
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
    rcfg.out_reliable_capacity = 8u;
    rcfg.out_unreliable_capacity = 64u;
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

    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 8u;
    ASSERT_EQ_INT(0, phys_world_init(&world, &wcfg));

    uint32_t body_idx = phys_world_create_body(&world);
    ASSERT_TRUE(body_idx != UINT32_MAX);

    phys_body_t *body = phys_world_get_body(&world, body_idx);
    ASSERT_TRUE(body != NULL);
    body->tier = 0u;
    body->position = (phys_vec3_t){1.2345f, 0.0f, 0.0f};
    body->linear_vel = (phys_vec3_t){0.0125f, 0.0f, 0.0f};
    body->angular_vel = (phys_vec3_t){0.0f, 0.0315f, 0.0f};

    fr_server_body_state_broadcast_config_t bcfg;
    memset(&bcfg, 0, sizeof(bcfg));
    bcfg.max_clients = 1u;
    bcfg.world = &world;
    bcfg.get_client_out_topics_cb = get_out_topics_from_runtime_;
    bcfg.io_user = rt;

    fr_server_body_state_broadcast_t *bcast = fr_server_body_state_broadcast_create(&bcfg);
    ASSERT_TRUE(bcast != NULL);

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

    ASSERT_EQ_INT(0, setup_connected_client_(t, rt, pump, bcast, cl));

    /* Tick 1: expect one BODY_STATE_BATCH message with correct quantization. */
    transport_advance_ms_(t, 1u);
    ASSERT_EQ_INT(0, drive_one_tick_(t, rt, pump, bcast, 1u, cl));

    uint16_t schema = 0u;
    uint8_t raw_payload[NET_REPL_BODY_STATE_BATCH_MAX_SIZE];
    size_t raw_payload_len = sizeof(raw_payload);
    ASSERT_TRUE(fr_test_client_pop_unreliable(cl, &schema, raw_payload, &raw_payload_len));
    ASSERT_EQ_U16(NET_REPL_SCHEMA_BODY_STATE_BATCH, schema);

    uint16_t batch_count = 0u;
    const uint8_t *entries = NULL;
    ASSERT_EQ_INT(NET_REPL_OK,
                  net_repl_body_state_batch_decode(raw_payload, raw_payload_len,
                                                   &batch_count, &entries));
    ASSERT_EQ_INT(1, (int)batch_count);

    net_repl_body_state_t st;
    memset(&st, 0, sizeof(st));
    ASSERT_EQ_INT(NET_REPL_OK,
                  net_repl_body_state_decode(&st, entries,
                                             NET_REPL_BODY_STATE_PAYLOAD_SIZE));
    ASSERT_EQ_U16(1u, st.server_tick);
    ASSERT_EQ_U16((uint16_t)body_idx, st.body_id);
    ASSERT_EQ_INT(1235, st.pos_mm.x_mm);
    ASSERT_EQ_INT(13, st.vel_x_mm_s);
    ASSERT_EQ_INT(32, st.ang_y_mrad_s);
    ASSERT_EQ_INT(0, (int)net_repl_body_state_tier(st.flags));

    fr_test_client_destroy(cl);
    fr_server_body_state_broadcast_destroy(bcast);
    phys_world_destroy(&world);
    fr_server_entity_net_pump_destroy(pump);
    fr_server_net_runtime_destroy(rt);
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    fr_test_transport_destroy(t);
    return 0;
}

static int test_body_state_broadcast_tiered_rate(void) {
    net_test_step_t steps[] = {{1u, 0u, 0u}};

    fr_test_transport_config_t tcfg;
    memset(&tcfg, 0, sizeof(tcfg));
    tcfg.max_clients = 1u;
    tcfg.base_port = 42020u;
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
    rcfg.out_reliable_capacity = 8u;
    rcfg.out_unreliable_capacity = 256u;
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

    phys_world_t world;
    phys_world_config_t wcfg = phys_world_config_default();
    wcfg.max_bodies = 8u;
    ASSERT_EQ_INT(0, phys_world_init(&world, &wcfg));

    uint32_t b0 = phys_world_create_body(&world);
    uint32_t b1 = phys_world_create_body(&world);
    ASSERT_TRUE(b0 != UINT32_MAX && b1 != UINT32_MAX);

    phys_body_t *body0 = phys_world_get_body(&world, b0);
    phys_body_t *body1 = phys_world_get_body(&world, b1);
    ASSERT_TRUE(body0 && body1);
    body0->tier = 0u;
    body1->tier = 2u; /* interval 4 ticks */

    fr_server_body_state_broadcast_config_t bcfg;
    memset(&bcfg, 0, sizeof(bcfg));
    bcfg.max_clients = 1u;
    bcfg.world = &world;
    bcfg.get_client_out_topics_cb = get_out_topics_from_runtime_;
    bcfg.io_user = rt;

    fr_server_body_state_broadcast_t *bcast = fr_server_body_state_broadcast_create(&bcfg);
    ASSERT_TRUE(bcast != NULL);

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

    ASSERT_EQ_INT(0, setup_connected_client_(t, rt, pump, bcast, cl));

    uint32_t got0 = 0u;
    uint32_t got1 = 0u;

    for (uint16_t tick = 0u; tick < 8u; ++tick) {
        transport_advance_ms_(t, 1u);
        ASSERT_EQ_INT(0, drive_one_tick_(t, rt, pump, bcast, tick, cl));

        for (;;) {
            uint16_t schema = 0u;
            uint8_t raw_payload[NET_REPL_BODY_STATE_BATCH_MAX_SIZE];
            size_t raw_payload_len = sizeof(raw_payload);
            if (!fr_test_client_pop_unreliable(cl, &schema, raw_payload, &raw_payload_len)) {
                break;
            }
            ASSERT_EQ_U16(NET_REPL_SCHEMA_BODY_STATE_BATCH, schema);

            uint16_t batch_count = 0u;
            const uint8_t *entries = NULL;
            ASSERT_EQ_INT(NET_REPL_OK,
                          net_repl_body_state_batch_decode(raw_payload, raw_payload_len,
                                                           &batch_count, &entries));

            for (uint16_t bi = 0u; bi < batch_count; ++bi) {
                net_repl_body_state_t st;
                memset(&st, 0, sizeof(st));
                ASSERT_EQ_INT(NET_REPL_OK,
                              net_repl_body_state_decode(
                                  &st,
                                  entries + (size_t)bi * NET_REPL_BODY_STATE_PAYLOAD_SIZE,
                                  NET_REPL_BODY_STATE_PAYLOAD_SIZE));

                if (st.body_id == (uint16_t)b0) {
                    got0++;
                } else if (st.body_id == (uint16_t)b1) {
                    got1++;
                } else {
                    TEST_FAIL("unexpected body_id %u", (unsigned)st.body_id);
                }
            }
        }
    }

    ASSERT_EQ_INT(8, (int)got0);
    ASSERT_EQ_INT(2, (int)got1);

    fr_test_client_destroy(cl);
    fr_server_body_state_broadcast_destroy(bcast);
    phys_world_destroy(&world);
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
    {"body_state_broadcast_sends_and_quantizes", test_body_state_broadcast_sends_and_quantizes},
    {"body_state_broadcast_tiered_rate", test_body_state_broadcast_tiered_rate},
};

int main(void) {
    size_t total = ARRAY_SIZE(TESTS);
    size_t passed = 0u;

    for (size_t i = 0u; i < total; ++i) {
        struct test_case *tc = &TESTS[i];
        printf("RUN %s\n", tc->name);
        int rc = tc->fn();
        if (rc == 0) {
            printf("PASS %s\n", tc->name);
            passed++;
        } else {
            printf("FAIL %s\n", tc->name);
            return 1;
        }
    }

    printf("%zu/%zu tests passed\n", passed, total);
    return 0;
}
