#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/topic_channel.h"

#include "ferrum/server/entity/net/pump.h"

#define TEST_FAIL(msg, ...)                                                                         \
    do {                                                                                            \
        fprintf(stderr, "FAIL %s:%d: " msg "\n", __FILE__, __LINE__, ##__VA_ARGS__);              \
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

static void write_u16_le(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8u) & 0xFFu);
}

static size_t build_inbound_join_msg(uint16_t client_id, uint32_t nonce, uint8_t *out, size_t cap) {
    net_repl_join_t join;
    join.client_nonce = nonce;

    uint8_t payload[NET_REPL_JOIN_PAYLOAD_SIZE];
    ASSERT_EQ_INT(0, net_repl_join_encode(&join, payload, sizeof(payload)));

    /* inbound msg format (from server net runtime):
       [client_id:u16 LE][schema_id:u16 LE][flags:u8][reserved:u8][payload...] */
    const size_t need = 6u + sizeof(payload);
    ASSERT_TRUE(cap >= need);

    write_u16_le(out + 0, client_id);
    write_u16_le(out + 2, NET_REPL_SCHEMA_JOIN);
    out[4] = 1u; /* reliable */
    out[5] = 0u;
    memcpy(out + 6u, payload, sizeof(payload));
    return need;
}

struct client_out_topics {
    fr_topic_channel_t *reliable;
    fr_topic_channel_t *unreliable;
};

struct test_env {
    struct client_out_topics clients[4];
};

static bool get_out_topics(void *user, uint16_t client_id, fr_topic_channel_t **out_rel, fr_topic_channel_t **out_unrel) {
    struct test_env *env = (struct test_env *)user;
    if (!env || !out_rel || !out_unrel) {
        return false;
    }
    if (client_id >= 4) {
        return false;
    }
    *out_rel = env->clients[client_id].reliable;
    *out_unrel = env->clients[client_id].unreliable;
    return (*out_rel != NULL && *out_unrel != NULL);
}

static int test_join_publishes_player_join_and_sends_welcome(void) {
    fr_topic_channel_config_t tcfg = {.capacity = 64};
    fr_topic_channel_t *inbound = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(inbound && player_events && entity_events);

    struct test_env env;
    memset(&env, 0, sizeof(env));
    env.clients[0].reliable = fr_topic_channel_create(&tcfg);
    env.clients[0].unreliable = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(env.clients[0].reliable && env.clients[0].unreliable);

    fr_server_entity_net_pump_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 4;
    cfg.tick_hz = 60;
    cfg.expected_entities = 1;
    cfg.inbound_topic = inbound;
    cfg.player_event_topic = player_events;
    cfg.entity_event_topic = entity_events;
    cfg.get_client_out_topics_cb = get_out_topics;
    cfg.io_user = &env;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&cfg);
    ASSERT_TRUE(pump != NULL);

    uint8_t msg[64];
    size_t msg_len = build_inbound_join_msg(0u, 0x12345678u, msg, sizeof(msg));
    ASSERT_TRUE(fr_topic_channel_push(inbound, msg, msg_len));

    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, 0u));

    /* Expect EVT_PLAYER_JOIN for client 0. */
    uint8_t evt[64];
    size_t evt_len = sizeof(evt);
    ASSERT_TRUE(fr_topic_channel_pop(player_events, evt, &evt_len));
    ASSERT_TRUE(evt_len >= 6u);
    ASSERT_EQ_INT(FR_SERVER_EVT_PLAYER_JOIN, evt[0]);
    uint16_t evt_client_id = (uint16_t)evt[2] | ((uint16_t)evt[3] << 8u);
    ASSERT_EQ_INT(0, evt_client_id);

    /* Expect WELCOME enqueued to client0 reliable out topic. */
    uint8_t out_msg[64];
    size_t out_len = sizeof(out_msg);
    ASSERT_TRUE(fr_topic_channel_pop(env.clients[0].reliable, out_msg, &out_len));
    ASSERT_TRUE(out_len >= 2u + NET_REPL_WELCOME_PAYLOAD_SIZE);
    uint16_t schema_id = (uint16_t)out_msg[0] | ((uint16_t)out_msg[1] << 8u);
    ASSERT_EQ_INT(NET_REPL_SCHEMA_WELCOME, schema_id);

    net_repl_welcome_t w;
    ASSERT_EQ_INT(0, net_repl_welcome_decode(&w, out_msg + 2u, out_len - 2u));
    ASSERT_EQ_INT(60, w.tick_hz);

    /* Expect SPAWN for self enqueued to client0 reliable out topic. */
    uint8_t spawn_msg[64];
    size_t spawn_len = sizeof(spawn_msg);
    ASSERT_TRUE(fr_topic_channel_pop(env.clients[0].reliable, spawn_msg, &spawn_len));
    ASSERT_TRUE(spawn_len >= 2u + NET_REPL_SPAWN_PAYLOAD_SIZE);
    uint16_t spawn_schema_id = (uint16_t)spawn_msg[0] | ((uint16_t)spawn_msg[1] << 8u);
    ASSERT_EQ_INT(NET_REPL_SCHEMA_SPAWN, spawn_schema_id);

    net_repl_spawn_t sp;
    ASSERT_EQ_INT(0, net_repl_spawn_decode(&sp, spawn_msg + 2u, spawn_len - 2u));
    ASSERT_EQ_INT(1000u, sp.entity_id);
    ASSERT_EQ_INT(0u, sp.owner_client_id);

    /* Ensure entity_events remains empty. */
    size_t e_len = sizeof(evt);
    ASSERT_TRUE(!fr_topic_channel_pop(entity_events, evt, &e_len));

    fr_server_entity_net_pump_destroy(pump);
    fr_topic_channel_destroy(env.clients[0].reliable);
    fr_topic_channel_destroy(env.clients[0].unreliable);
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    return 0;
}

static int test_second_join_sends_cross_spawns_and_events(void) {
    fr_topic_channel_config_t tcfg = {.capacity = 64};
    fr_topic_channel_t *inbound = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(inbound && player_events && entity_events);

    struct test_env env;
    memset(&env, 0, sizeof(env));
    for (uint16_t i = 0u; i < 2u; ++i) {
        env.clients[i].reliable = fr_topic_channel_create(&tcfg);
        env.clients[i].unreliable = fr_topic_channel_create(&tcfg);
        ASSERT_TRUE(env.clients[i].reliable && env.clients[i].unreliable);
    }

    fr_server_entity_net_pump_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 4;
    cfg.tick_hz = 60;
    cfg.expected_entities = 2;
    cfg.inbound_topic = inbound;
    cfg.player_event_topic = player_events;
    cfg.entity_event_topic = entity_events;
    cfg.get_client_out_topics_cb = get_out_topics;
    cfg.io_user = &env;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&cfg);
    ASSERT_TRUE(pump != NULL);

    /* Join client 0. */
    uint8_t msg0[64];
    size_t msg0_len = build_inbound_join_msg(0u, 0x11111111u, msg0, sizeof(msg0));
    ASSERT_TRUE(fr_topic_channel_push(inbound, msg0, msg0_len));
    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, 0u));

    /* Drain welcome for client0 (not under test here). */
    uint8_t drain[128];
    size_t drain_len = sizeof(drain);
    (void)fr_topic_channel_pop(env.clients[0].reliable, drain, &drain_len);
    drain_len = sizeof(drain);
    (void)fr_topic_channel_pop(env.clients[0].reliable, drain, &drain_len);

    /* Join client 1. */
    uint8_t msg1[64];
    size_t msg1_len = build_inbound_join_msg(1u, 0x22222222u, msg1, sizeof(msg1));
    ASSERT_TRUE(fr_topic_channel_push(inbound, msg1, msg1_len));
    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, 0u));

    /* Expect client0 receives SPAWN for player1. */
    uint8_t out0[128];
    size_t out0_len = sizeof(out0);
    ASSERT_TRUE(fr_topic_channel_pop(env.clients[0].reliable, out0, &out0_len));
    ASSERT_TRUE(out0_len >= 2u + NET_REPL_SPAWN_PAYLOAD_SIZE);
    uint16_t schema0 = (uint16_t)out0[0] | ((uint16_t)out0[1] << 8u);
    ASSERT_EQ_INT(NET_REPL_SCHEMA_SPAWN, schema0);

    net_repl_spawn_t sp0;
    ASSERT_EQ_INT(0, net_repl_spawn_decode(&sp0, out0 + 2u, out0_len - 2u));
    ASSERT_EQ_INT(1001u, sp0.entity_id);
    ASSERT_EQ_INT(1u, sp0.owner_client_id);

    /* Expect client1 receives WELCOME and two SPAWNs (self + player0). */
    uint8_t out1[128];
    size_t out1_len = sizeof(out1);
    ASSERT_TRUE(fr_topic_channel_pop(env.clients[1].reliable, out1, &out1_len));
    ASSERT_TRUE(out1_len >= 2u + NET_REPL_WELCOME_PAYLOAD_SIZE);
    uint16_t schema1 = (uint16_t)out1[0] | ((uint16_t)out1[1] << 8u);
    ASSERT_EQ_INT(NET_REPL_SCHEMA_WELCOME, schema1);

    int saw_self = 0;
    int saw_other = 0;
    for (int i = 0; i < 2; ++i) {
        out1_len = sizeof(out1);
        ASSERT_TRUE(fr_topic_channel_pop(env.clients[1].reliable, out1, &out1_len));
        ASSERT_TRUE(out1_len >= 2u + NET_REPL_SPAWN_PAYLOAD_SIZE);
        schema1 = (uint16_t)out1[0] | ((uint16_t)out1[1] << 8u);
        ASSERT_EQ_INT(NET_REPL_SCHEMA_SPAWN, schema1);

        net_repl_spawn_t sp;
        ASSERT_EQ_INT(0, net_repl_spawn_decode(&sp, out1 + 2u, out1_len - 2u));
        if (sp.owner_client_id == 1u) {
            ASSERT_EQ_INT(1001u, sp.entity_id);
            saw_self = 1;
        } else if (sp.owner_client_id == 0u) {
            ASSERT_EQ_INT(1000u, sp.entity_id);
            saw_other = 1;
        }
    }
    ASSERT_TRUE(saw_self);
    ASSERT_TRUE(saw_other);

    /* Expect at least one EVT_PLAYER_SPAWN. */
    uint8_t evt[64];
    size_t evt_len = sizeof(evt);
    int saw_player_spawn_evt = 0;
    while (fr_topic_channel_pop(player_events, evt, &evt_len)) {
        if (evt_len >= 1u && evt[0] == FR_SERVER_EVT_PLAYER_SPAWN) {
            saw_player_spawn_evt = 1;
            break;
        }
        evt_len = sizeof(evt);
    }
    ASSERT_TRUE(saw_player_spawn_evt);

    fr_server_entity_net_pump_destroy(pump);
    for (uint16_t i = 0u; i < 2u; ++i) {
        fr_topic_channel_destroy(env.clients[i].reliable);
        fr_topic_channel_destroy(env.clients[i].unreliable);
    }
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    return 0;
}

static int test_spawn_suppressed_when_player_should_not_spawn_remote(void) {
    fr_topic_channel_config_t tcfg = {.capacity = 64};
    fr_topic_channel_t *inbound = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *player_events = fr_topic_channel_create(&tcfg);
    fr_topic_channel_t *entity_events = fr_topic_channel_create(&tcfg);
    ASSERT_TRUE(inbound && player_events && entity_events);

    struct test_env env;
    memset(&env, 0, sizeof(env));
    for (uint16_t i = 0u; i < 2u; ++i) {
        env.clients[i].reliable = fr_topic_channel_create(&tcfg);
        env.clients[i].unreliable = fr_topic_channel_create(&tcfg);
        ASSERT_TRUE(env.clients[i].reliable && env.clients[i].unreliable);
    }

    fr_server_entity_net_pump_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.max_clients = 4;
    cfg.tick_hz = 60;
    cfg.expected_entities = 2;
    cfg.inbound_topic = inbound;
    cfg.player_event_topic = player_events;
    cfg.entity_event_topic = entity_events;
    cfg.get_client_out_topics_cb = get_out_topics;
    cfg.io_user = &env;

    fr_server_entity_net_pump_t *pump = fr_server_entity_net_pump_create(&cfg);
    ASSERT_TRUE(pump != NULL);

    /* Join client 0, then mark it as not spawnable to others. */
    uint8_t msg0[64];
    size_t msg0_len = build_inbound_join_msg(0u, 0x33333333u, msg0, sizeof(msg0));
    ASSERT_TRUE(fr_topic_channel_push(inbound, msg0, msg0_len));
    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, 0u));
    ASSERT_TRUE(fr_server_entity_net_pump_set_player_should_spawn_remote(pump, 0u, false));

    /* Drain any messages already queued. */
    uint8_t drain[128];
    size_t drain_len = sizeof(drain);
    while (fr_topic_channel_pop(env.clients[0].reliable, drain, &drain_len)) {
        drain_len = sizeof(drain);
    }

    /* Join client 1: may receive SPAWN for self, but should NOT receive SPAWN for player 0. */
    uint8_t msg1[64];
    size_t msg1_len = build_inbound_join_msg(1u, 0x44444444u, msg1, sizeof(msg1));
    ASSERT_TRUE(fr_topic_channel_push(inbound, msg1, msg1_len));
    ASSERT_TRUE(fr_server_entity_net_pump_tick(pump, 0u));

    uint8_t out[128];
    size_t out_len = sizeof(out);
    int saw_spawn_for_player0 = 0;
    while (fr_topic_channel_pop(env.clients[1].reliable, out, &out_len)) {
        uint16_t schema = (uint16_t)out[0] | ((uint16_t)out[1] << 8u);
        if (schema == NET_REPL_SCHEMA_SPAWN) {
            net_repl_spawn_t sp;
            ASSERT_EQ_INT(0, net_repl_spawn_decode(&sp, out + 2u, out_len - 2u));
            if (sp.owner_client_id == 0u) {
                saw_spawn_for_player0 = 1;
                break;
            }
        }
        out_len = sizeof(out);
    }
    ASSERT_TRUE(!saw_spawn_for_player0);

    fr_server_entity_net_pump_destroy(pump);
    for (uint16_t i = 0u; i < 2u; ++i) {
        fr_topic_channel_destroy(env.clients[i].reliable);
        fr_topic_channel_destroy(env.clients[i].unreliable);
    }
    fr_topic_channel_destroy(inbound);
    fr_topic_channel_destroy(player_events);
    fr_topic_channel_destroy(entity_events);
    return 0;
}

int main(void) {
    struct {
        const char *name;
        int (*fn)(void);
    } tests[] = {
        {"join_publishes_player_join_and_sends_welcome", test_join_publishes_player_join_and_sends_welcome},
        {"second_join_sends_cross_spawns_and_events", test_second_join_sends_cross_spawns_and_events},
        {"spawn_suppressed_when_player_should_not_spawn_remote", test_spawn_suppressed_when_player_should_not_spawn_remote},
    };

    for (size_t i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
        fprintf(stdout, "RUN %s\n", tests[i].name);
        int rc = tests[i].fn();
        if (rc != 0) {
            return rc;
        }
        fprintf(stdout, "OK %s\n", tests[i].name);
    }
    fprintf(stdout, "All %zu tests passed\n", sizeof(tests) / sizeof(tests[0]));
    return 0;
}
