#include <stdlib.h>
#include <string.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/server/net/inbound_message.h"
#include "ferrum/server/entity/net/pump.h"

struct fr_server_entity_net_pump {
    fr_server_entity_net_pump_config_t cfg;

    struct player_slot {
        bool joined;
        uint16_t player_id;
        bool should_spawn_remote;
    } *players;

    /* spawned[dst_client_id * max_clients + src_player_id] == 1 when we've
       enqueued a SPAWN for src_player_id to dst_client_id.
     */
    uint8_t *spawned;
};

static void write_u16_le_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8u) & 0xFFu);
}

static bool publish_player_event_(fr_server_entity_net_pump_t *pump,
                                 uint8_t evt_type,
                                 uint16_t dst_client_id,
                                 uint16_t player_id) {
    if (!pump || !pump->cfg.player_event_topic) {
        return false;
    }

    /* Event msg format:
       [evt_type:u8][reserved:u8][client_id:u16 LE][player_id:u16 LE]
     */
    uint8_t evt[6];
    evt[0] = evt_type;
    evt[1] = 0u;
    write_u16_le_(evt + 2, dst_client_id);
    write_u16_le_(evt + 4, player_id);
    return fr_topic_channel_push(pump->cfg.player_event_topic, evt, sizeof(evt));
}

static bool enqueue_reliable_(fr_server_entity_net_pump_t *pump,
                             uint16_t client_id,
                             uint16_t schema_id,
                             const uint8_t *payload,
                             size_t payload_size) {
    if (!pump || !pump->cfg.get_client_out_topics_cb || !payload) {
        return false;
    }

    fr_topic_channel_t *out_rel = NULL;
    fr_topic_channel_t *out_unrel = NULL;
    (void)pump->cfg.get_client_out_topics_cb(pump->cfg.io_user, client_id, &out_rel, &out_unrel);
    if (!out_rel) {
        return false;
    }

    uint8_t msg[2u + NET_RUDP_MAX_PACKET_SIZE];
    if (payload_size > NET_RUDP_MAX_PACKET_SIZE) {
        return false;
    }
    msg[0] = (uint8_t)(schema_id & 0xFFu);
    msg[1] = (uint8_t)((schema_id >> 8u) & 0xFFu);
    memcpy(msg + 2u, payload, payload_size);
    return fr_topic_channel_push(out_rel, msg, 2u + payload_size);
}

static bool ensure_spawn_(fr_server_entity_net_pump_t *pump, uint16_t dst_client_id, uint16_t src_player_id) {
    if (!pump || !pump->players || !pump->spawned) {
        return false;
    }
    if (dst_client_id >= pump->cfg.max_clients || src_player_id >= pump->cfg.max_clients) {
        return false;
    }
    if (!pump->players[dst_client_id].joined || !pump->players[src_player_id].joined) {
        return false;
    }
    if (dst_client_id != src_player_id && !pump->players[src_player_id].should_spawn_remote) {
        return false;
    }

    const size_t idx = (size_t)dst_client_id * (size_t)pump->cfg.max_clients + (size_t)src_player_id;
    if (pump->spawned[idx]) {
        return true;
    }

    net_repl_spawn_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.entity_id = 1000u + (uint32_t)src_player_id;
    sp.owner_client_id = src_player_id;
    sp.join_time_u16 = 0u;
    sp.pos_mm = (net_repl_vec3_mm_t){0, 0, 0};

    uint8_t sp_payload[NET_REPL_SPAWN_PAYLOAD_SIZE];
    if (net_repl_spawn_encode(&sp, sp_payload, sizeof(sp_payload)) != NET_REPL_OK) {
        return false;
    }

    if (!enqueue_reliable_(pump, dst_client_id, NET_REPL_SCHEMA_SPAWN, sp_payload, sizeof(sp_payload))) {
        return false;
    }

    pump->spawned[idx] = 1u;
    (void)publish_player_event_(pump, (uint8_t)FR_SERVER_EVT_PLAYER_SPAWN, dst_client_id, src_player_id);
    return true;
}

fr_server_entity_net_pump_t *fr_server_entity_net_pump_create(const fr_server_entity_net_pump_config_t *cfg) {
    if (!cfg || cfg->max_clients == 0u) {
        return NULL;
    }
    if (!cfg->inbound_topic || !cfg->player_event_topic || !cfg->entity_event_topic) {
        return NULL;
    }
    if (!cfg->get_client_out_topics_cb) {
        return NULL;
    }

    fr_server_entity_net_pump_t *pump = (fr_server_entity_net_pump_t *)calloc(1u, sizeof(*pump));
    if (!pump) {
        return NULL;
    }
    pump->cfg = *cfg;

    pump->players = (struct player_slot *)calloc((size_t)cfg->max_clients, sizeof(pump->players[0]));
    if (!pump->players) {
        free(pump);
        return NULL;
    }

    pump->spawned = (uint8_t *)calloc((size_t)cfg->max_clients * (size_t)cfg->max_clients, 1u);
    if (!pump->spawned) {
        free(pump->players);
        free(pump);
        return NULL;
    }

    return pump;
}

void fr_server_entity_net_pump_destroy(fr_server_entity_net_pump_t *pump) {
    if (!pump) {
        return;
    }
    free(pump->players);
    free(pump->spawned);
    free(pump);
}

bool fr_server_entity_net_pump_set_player_should_spawn_remote(fr_server_entity_net_pump_t *pump,
                                                              uint16_t client_id,
                                                              bool should_spawn_remote) {
    if (!pump || !pump->players || client_id >= pump->cfg.max_clients) {
        return false;
    }
    if (!pump->players[client_id].joined) {
        return false;
    }
    pump->players[client_id].should_spawn_remote = should_spawn_remote;
    return true;
}

bool fr_server_entity_net_pump_tick(fr_server_entity_net_pump_t *pump, uint64_t now_ms) {
    (void)now_ms;
    if (!pump || !pump->players || !pump->spawned) {
        return false;
    }

    uint8_t in[256];

    for (;;) {
        size_t in_len = sizeof(in);
        if (!fr_topic_channel_pop(pump->cfg.inbound_topic, in, &in_len)) {
            break;
        }

        fr_server_net_inbound_message_view_t msg;
        memset(&msg, 0, sizeof(msg));
        if (!fr_server_net_inbound_message_decode(&msg, in, in_len)) {
            continue;
        }

        const uint16_t client_id = msg.client_id;
        const uint16_t schema_id = msg.schema_id;
        const uint8_t *payload = msg.payload;
        const size_t payload_size = msg.payload_size;

        if (client_id >= pump->cfg.max_clients) {
            continue;
        }

        if (schema_id != NET_REPL_SCHEMA_JOIN) {
            continue;
        }

        net_repl_join_t join;
        if (net_repl_join_decode(&join, payload, payload_size) != NET_REPL_OK) {
            continue;
        }
        (void)join;

        /* Idempotent join: only process first join per client_id. */
        if (!pump->players[client_id].joined) {
            pump->players[client_id].joined = true;
            pump->players[client_id].player_id = client_id;
            pump->players[client_id].should_spawn_remote = true;

            (void)publish_player_event_(pump, (uint8_t)FR_SERVER_EVT_PLAYER_JOIN, client_id, pump->players[client_id].player_id);

            net_repl_welcome_t w;
            w.expected_entities = pump->cfg.expected_entities;
            w.tick_hz = pump->cfg.tick_hz;
            uint8_t w_payload[NET_REPL_WELCOME_PAYLOAD_SIZE];
            if (net_repl_welcome_encode(&w, w_payload, sizeof(w_payload)) == NET_REPL_OK) {
                (void)enqueue_reliable_(pump, client_id, NET_REPL_SCHEMA_WELCOME, w_payload, sizeof(w_payload));
            }

            /* Spawn self to self so clients always receive their own entity. */
            (void)ensure_spawn_(pump, client_id, pump->players[client_id].player_id);

            /* Cross-spawn between this player and already-joined players.
               This is the first place interest/visibility gating is applied.
             */
            for (uint16_t other = 0u; other < pump->cfg.max_clients; ++other) {
                if (other == client_id) {
                    continue;
                }
                if (!pump->players[other].joined) {
                    continue;
                }

                (void)ensure_spawn_(pump, client_id, pump->players[other].player_id);
                (void)ensure_spawn_(pump, other, pump->players[client_id].player_id);
            }
        }
    }

    /* Reconcile: ensure every joined client has SPAWNs for all joined players. */
    for (uint16_t dst = 0u; dst < pump->cfg.max_clients; ++dst) {
        if (!pump->players[dst].joined) {
            continue;
        }
        for (uint16_t src = 0u; src < pump->cfg.max_clients; ++src) {
            if (!pump->players[src].joined) {
                continue;
            }
            (void)ensure_spawn_(pump, dst, src);
        }
    }

    return true;
}
