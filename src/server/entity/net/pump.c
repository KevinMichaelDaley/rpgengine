#include <stdlib.h>
#include <string.h>

#include "ferrum/net/replication/common.h"
#include "ferrum/net/replication/event_batch.h"
#include "ferrum/net/replication/input_rot.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/quantization.h"
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

    /* Scratch buffers for per-tick spawn batching (allocated once, reused). */
    net_repl_event_entry_view_t *scratch_entries;
    uint16_t *scratch_src_player_ids;
    uint8_t *scratch_spawn_payloads;
    uint8_t *scratch_event_payload;
};

static void write_u16_le_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8u) & 0xFFu);
}

static void write_i16_le_(uint8_t *out, int16_t v) {
    write_u16_le_(out, (uint16_t)v);
}

static void write_u32_le_(uint8_t *out, uint32_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8u) & 0xFFu);
    out[2] = (uint8_t)((v >> 16u) & 0xFFu);
    out[3] = (uint8_t)((v >> 24u) & 0xFFu);
}

static vec3_t player_spawn_pos_(uint16_t owner_client_id, uint16_t max_clients) {
    const float spacing = 0.75f;
    const float center = (max_clients > 0u) ? ((float)(max_clients - 1u) * 0.5f) : 0.0f;
    const float x = ((float)owner_client_id - center) * spacing;
    return (vec3_t){x, 0.0f, 0.0f};
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

static bool publish_entity_input_rot_event_(fr_server_entity_net_pump_t *pump,
                                            uint16_t src_client_id,
                                            uint32_t entity_id,
                                            uint32_t event_id,
                                            int16_t axis_x_snorm16,
                                            int16_t axis_y_snorm16,
                                            int16_t axis_z_snorm16,
                                            uint16_t speed_millirad_per_s) {
    if (!pump || !pump->cfg.entity_event_topic) {
        return false;
    }

    /* Event msg format:
       [evt_type:u8][reserved:u8][src_client_id:u16 LE]
       [entity_id:u32 LE][event_id:u32 LE]
       [axis_x:i16 LE][axis_y:i16 LE][axis_z:i16 LE]
       [speed_millirad_per_s:u16 LE]
     */
    uint8_t evt[20];
    evt[0] = (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_ROT;
    evt[1] = 0u;
    write_u16_le_(evt + 2u, src_client_id);
    write_u32_le_(evt + 4u, entity_id);
    write_u32_le_(evt + 8u, event_id);
    write_i16_le_(evt + 12u, axis_x_snorm16);
    write_i16_le_(evt + 14u, axis_y_snorm16);
    write_i16_le_(evt + 16u, axis_z_snorm16);
    write_u16_le_(evt + 18u, speed_millirad_per_s);

    return fr_topic_channel_push(pump->cfg.entity_event_topic, evt, sizeof(evt));
}

static bool build_spawn_payload_(fr_server_entity_net_pump_t *pump,
                                 uint16_t src_player_id,
                                 uint8_t *out_payload,
                                 size_t out_capacity) {
    if (!pump || !out_payload || out_capacity < (size_t)NET_REPL_SPAWN_PAYLOAD_SIZE) {
        return false;
    }

    net_repl_spawn_t sp;
    memset(&sp, 0, sizeof(sp));
    sp.entity_id = 1000u + (uint32_t)src_player_id;
    sp.owner_client_id = src_player_id;
    sp.join_time_u16 = 0u;

    net_qvec3_mm_t qpos;
    const vec3_t pos = player_spawn_pos_(src_player_id, pump->cfg.max_clients);
    if (net_quantize_vec3_mm(pos, &qpos) != NET_QUANT_OK) {
        return false;
    }
    sp.pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};

    return net_repl_spawn_encode(&sp, out_payload, out_capacity) == NET_REPL_OK;
}

static bool reconcile_spawns_for_dst_(fr_server_entity_net_pump_t *pump, uint16_t dst_client_id) {
    if (!pump || !pump->players || !pump->spawned) {
        return false;
    }
    if (dst_client_id >= pump->cfg.max_clients) {
        return false;
    }
    if (!pump->players[dst_client_id].joined) {
        return true;
    }
    if (!pump->scratch_entries || !pump->scratch_src_player_ids || !pump->scratch_spawn_payloads || !pump->scratch_event_payload) {
        return false;
    }

    /* Build a list of missing SPAWNs for this destination client, then send
       them as one or more NET_REPL_SCHEMA_EVENT batches.
     */
    uint16_t pending = 0u;
    for (uint16_t src = 0u; src < pump->cfg.max_clients; ++src) {
        if (!pump->players[src].joined) {
            continue;
        }
        if (dst_client_id != src && !pump->players[src].should_spawn_remote) {
            continue;
        }

        const size_t idx = (size_t)dst_client_id * (size_t)pump->cfg.max_clients + (size_t)src;
        if (pump->spawned[idx]) {
            continue;
        }

        uint8_t *sp_payload = pump->scratch_spawn_payloads + (size_t)pending * (size_t)NET_REPL_SPAWN_PAYLOAD_SIZE;
        if (!build_spawn_payload_(pump, src, sp_payload, NET_REPL_SPAWN_PAYLOAD_SIZE)) {
            continue;
        }

        pump->scratch_entries[pending] = (net_repl_event_entry_view_t){
            .type = NET_REPL_EVENT_SPAWN,
            .entity_key = (uint64_t)(1000u + (uint32_t)src),
            .payload = sp_payload,
            .payload_size = NET_REPL_SPAWN_PAYLOAD_SIZE,
        };
        pump->scratch_src_player_ids[pending] = src;
        pending++;
    }

    uint16_t offset = 0u;
    while (offset < pending) {
        /* Fit as many entries as possible in a single stream message payload. */
        size_t cap = (size_t)NET_RUDP_MAX_PACKET_SIZE;
        size_t total = 4u;
        uint16_t count = 0u;
        while (offset + count < pending) {
            const net_repl_event_entry_view_t *e = &pump->scratch_entries[offset + count];
            const size_t need = 12u + (size_t)e->payload_size;
            if (total + need > cap) {
                break;
            }
            total += need;
            count++;
        }
        if (count == 0u) {
            break;
        }

        size_t payload_size = 0u;
        if (net_repl_event_batch_encode(0u,
                                       pump->scratch_entries + offset,
                                       count,
                                       pump->scratch_event_payload,
                                       NET_RUDP_MAX_PACKET_SIZE,
                                       &payload_size) != NET_REPL_OK) {
            break;
        }

        if (!enqueue_reliable_(pump,
                               dst_client_id,
                               NET_REPL_SCHEMA_EVENT,
                               pump->scratch_event_payload,
                               payload_size)) {
            break; /* backpressure; retry next tick */
        }

        for (uint16_t i = 0u; i < count; ++i) {
            const uint16_t src = pump->scratch_src_player_ids[offset + i];
            const size_t idx = (size_t)dst_client_id * (size_t)pump->cfg.max_clients + (size_t)src;
            pump->spawned[idx] = 1u;
            (void)publish_player_event_(pump, (uint8_t)FR_SERVER_EVT_PLAYER_SPAWN, dst_client_id, src);
        }

        offset = (uint16_t)(offset + count);
    }

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

    pump->scratch_entries = (net_repl_event_entry_view_t *)calloc((size_t)cfg->max_clients, sizeof(*pump->scratch_entries));
    pump->scratch_src_player_ids = (uint16_t *)calloc((size_t)cfg->max_clients, sizeof(*pump->scratch_src_player_ids));
    pump->scratch_spawn_payloads = (uint8_t *)calloc((size_t)cfg->max_clients * (size_t)NET_REPL_SPAWN_PAYLOAD_SIZE, 1u);
    pump->scratch_event_payload = (uint8_t *)calloc((size_t)NET_RUDP_MAX_PACKET_SIZE, 1u);

    if (!pump->scratch_entries || !pump->scratch_src_player_ids || !pump->scratch_spawn_payloads || !pump->scratch_event_payload) {
        free(pump->scratch_entries);
        free(pump->scratch_src_player_ids);
        free(pump->scratch_spawn_payloads);
        free(pump->scratch_event_payload);
        free(pump->spawned);
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
    free(pump->scratch_entries);
    free(pump->scratch_src_player_ids);
    free(pump->scratch_spawn_payloads);
    free(pump->scratch_event_payload);
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

        if (schema_id == NET_REPL_SCHEMA_JOIN) {
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

                /* SPAWNs are produced by the end-of-tick reconcile pass so they
                   can be batched and retried under backpressure.
                 */
            }

            continue;
        }

        if (schema_id == NET_REPL_SCHEMA_INPUT_ROT) {
            net_repl_input_rot_t ev;
            memset(&ev, 0, sizeof(ev));
            if (net_repl_input_rot_decode(&ev, payload, payload_size) != NET_REPL_OK) {
                continue;
            }

            /* Ownership gate for this harness: entity_id is 1000 + owner_client_id.
               Allow entity_id==0 as "my entity" so clients don't need to know their assigned id.
             */
            const uint32_t expected_entity_id = 1000u + (uint32_t)client_id;
            if (ev.entity_id == 0u) {
                ev.entity_id = expected_entity_id;
            }
            if (ev.entity_id != expected_entity_id) {
                continue;
            }

            (void)publish_entity_input_rot_event_(pump,
                                                  client_id,
                                                  ev.entity_id,
                                                  ev.event_id,
                                                  ev.axis_x_snorm16,
                                                  ev.axis_y_snorm16,
                                                  ev.axis_z_snorm16,
                                                  ev.speed_millirad_per_s);
            continue;
        }

        /* Forward INPUT_MOVE and INPUT_SPAWN to entity_event_topic for
           game-specific processing (e.g. demo server world).
           Event format: [evt_type:u8][reserved:u8][client_id:u16 LE][payload...]
         */
        if (schema_id == NET_REPL_SCHEMA_INPUT_MOVE || schema_id == NET_REPL_SCHEMA_INPUT_SPAWN) {
            const uint8_t evt_type = (schema_id == NET_REPL_SCHEMA_INPUT_MOVE)
                                         ? (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_MOVE
                                         : (uint8_t)FR_SERVER_EVT_ENTITY_INPUT_SPAWN;
            if (payload_size <= 252u && pump->cfg.entity_event_topic) {
                uint8_t fwd[256];
                fwd[0] = evt_type;
                fwd[1] = 0u;
                write_u16_le_(fwd + 2u, client_id);
                memcpy(fwd + 4u, payload, payload_size);
                (void)fr_topic_channel_push(pump->cfg.entity_event_topic, fwd, 4u + payload_size);
            }
            continue;
        }

        /* Unknown schema IDs are ignored by this pump. */
        continue;
    }

    /* Reconcile: ensure every joined client has SPAWNs for all joined players.
       This runs every tick so missed spawns (backpressure) will retry.
     */
    for (uint16_t dst = 0u; dst < pump->cfg.max_clients; ++dst) {
        (void)reconcile_spawns_for_dst_(pump, dst);
    }

    return true;
}
