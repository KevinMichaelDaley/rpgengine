#include <math.h>
#include <stdlib.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/quantization.h"
#include "ferrum/net/replication/spawn_batch.h"
#include "ferrum/net/replication/state_cube.h"
#include "ferrum/net/replication/welcome.h"
#include "ferrum/net/rudp/peer.h"
#include "internal.h"

static uint8_t bitset_test(const uint8_t *bits, uint16_t bit_index) {
    const size_t byte_index = (size_t)bit_index >> 3u;
    const uint8_t mask = (uint8_t)(1u << (bit_index & 7u));
    return (uint8_t)((bits[byte_index] & mask) != 0u);
}

static void bitset_set(uint8_t *bits, uint16_t bit_index) {
    const size_t byte_index = (size_t)bit_index >> 3u;
    const uint8_t mask = (uint8_t)(1u << (bit_index & 7u));
    bits[byte_index] = (uint8_t)(bits[byte_index] | mask);
}

static int server_repl_try_send_welcome(server_repl_server_t *srv, uint16_t client_id, uint64_t now_ms) {
    net_repl_welcome_t msg;
    msg.expected_entities = srv->cfg.max_entities;
    msg.tick_hz = srv->cfg.tick_hz;

    uint8_t payload[NET_REPL_WELCOME_PAYLOAD_SIZE];
    if (net_repl_welcome_encode(&msg, payload, sizeof(payload)) != NET_REPL_OK) {
        return SERVER_REPL_OK;
    }

    int rc = net_rudp_peer_send_reliable(&srv->clients[client_id].peer,
                                         srv->sock,
                                         &srv->clients[client_id].addr,
                                         now_ms,
                                         NET_REPL_SCHEMA_WELCOME,
                                         payload,
                                         sizeof(payload),
                                         NULL);
    if (rc == NET_RUDP_OK) {
        srv->clients[client_id].welcome_sent = 1u;
        srv->stats.packets_sent++;
        srv->stats.bytes_sent += (uint64_t)(NET_PACKET_HEADER_SIZE + 8u + sizeof(payload));
        return SERVER_REPL_OK;
    }
    if (rc == NET_RUDP_ERR_FULL) {
        return NET_RUDP_ERR_FULL;
    }
    return SERVER_REPL_OK;
}

static int server_repl_try_send_spawn_batch(server_repl_server_t *srv,
                                            uint16_t client_id,
                                            uint16_t max_batches_this_tick,
                                            net_repl_spawn_batch_entry_t *entries,
                                            uint16_t *entry_entity_indices,
                                            uint16_t entry_capacity,
                                            uint8_t *payload,
                                            size_t payload_capacity,
                                            uint64_t now_ms) {
    if (!srv->entity_known_bits || srv->entity_known_stride_bytes == 0u || !srv->spawn_cursor) {
        return SERVER_REPL_OK;
    }

    const size_t known_stride = srv->entity_known_stride_bytes;
    uint8_t *known = srv->entity_known_bits + ((size_t)client_id * known_stride);

    const uint16_t total_entities = srv->cfg.max_entities;
    uint16_t cursor = srv->spawn_cursor[client_id];

    for (uint16_t batch = 0u; batch < max_batches_this_tick; ++batch) {
        uint16_t count = 0u;
        uint16_t scanned = 0u;

        while (count < entry_capacity && scanned < total_entities) {
            const uint16_t ei = (uint16_t)((cursor + scanned) % total_entities);
            scanned++;

            if (!srv->entities[ei].active) {
                continue;
            }
            if (bitset_test(known, ei)) {
                continue;
            }

            const uint16_t owner = srv->entities[ei].owner_client_id;
            const float t = (float)srv->server_tick / (float)srv->cfg.tick_hz;
            const float phase = (float)owner * 0.25f;
            vec3_t pos = (vec3_t){cosf(t + phase), 0.0f, sinf(t + phase)};
            net_qvec3_mm_t qpos;
            if (net_quantize_vec3_mm(pos, &qpos) != NET_QUANT_OK) {
                continue;
            }

            entries[count] = (net_repl_spawn_batch_entry_t){
                .entity_id = srv->entities[ei].entity_id,
                .owner_client_id = owner,
                .pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm},
            };
            entry_entity_indices[count] = ei;
            count++;
        }

        cursor = (uint16_t)((cursor + scanned) % total_entities);
        srv->spawn_cursor[client_id] = cursor;

        if (count == 0u) {
            return SERVER_REPL_OK;
        }

        size_t payload_size = 0u;
        if (net_repl_spawn_batch_encode(srv->server_tick, entries, count, payload, payload_capacity, &payload_size) != NET_REPL_OK) {
            return SERVER_REPL_OK;
        }

        int rc = net_rudp_peer_send_reliable(&srv->clients[client_id].peer,
                                             srv->sock,
                                             &srv->clients[client_id].addr,
                                             now_ms,
                                             NET_REPL_SCHEMA_SPAWN_BATCH,
                                             payload,
                                             payload_size,
                                             NULL);
        if (rc == NET_RUDP_OK) {
            for (uint16_t i = 0u; i < count; ++i) {
                bitset_set(known, entry_entity_indices[i]);
            }
            srv->stats.packets_sent++;
            srv->stats.bytes_sent += (uint64_t)(NET_PACKET_HEADER_SIZE + 8u + payload_size);
            continue;
        }
        if (rc == NET_RUDP_ERR_FULL) {
            return NET_RUDP_ERR_FULL;
        }
        return SERVER_REPL_OK;
    }

    return SERVER_REPL_OK;
}

static void send_state_job(void *user_data) {
    struct server_repl_send_job_ctx *ctx = (struct server_repl_send_job_ctx *)user_data;
    server_repl_server_t *srv = ctx->srv;
    uint16_t ci = ctx->client_index;
    uint16_t ei = ctx->entity_index;

    if (!srv->clients[ci].active || !srv->clients[ci].joined || !srv->entities[ei].active) {
        return;
    }

    const float t = (float)srv->server_tick / (float)srv->cfg.tick_hz;
    const float phase = (float)srv->entities[ei].owner_client_id * 0.25f;
    vec3_t pos = (vec3_t){cosf(t + phase), 0.0f, sinf(t + phase)};
    quat_t rot = (quat_t){0.0f, 0.0f, 0.0f, 1.0f};

    net_qvec3_mm_t qpos;
    net_qquat_snorm16_t qrot;
    if (net_quantize_vec3_mm(pos, &qpos) != NET_QUANT_OK) {
        return;
    }
    if (net_quantize_quat_snorm16(rot, &qrot) != NET_QUANT_OK) {
        return;
    }

    net_repl_state_cube_t st;
    st.server_tick = srv->server_tick;
    st.entity_id = srv->entities[ei].entity_id;
    st.pos_mm = (net_repl_vec3_mm_t){qpos.x_mm, qpos.y_mm, qpos.z_mm};
    st.rot_snorm16 = (net_repl_quat_snorm16_t){qrot.x, qrot.y, qrot.z, qrot.w};

    uint8_t payload[NET_REPL_STATE_CUBE_PAYLOAD_SIZE];
    if (net_repl_state_cube_encode(&st, payload, sizeof(payload)) != NET_REPL_OK) {
        return;
    }

    (void)net_rudp_peer_send_unreliable(&srv->clients[ci].peer,
                                        srv->sock,
                                        &srv->clients[ci].addr,
                                        ctx->now_ms,
                                        NET_REPL_SCHEMA_STATE_CUBE,
                                        payload,
                                        sizeof(payload));
}

static void server_repl_send_some_states(server_repl_server_t *srv, uint16_t client_id, uint16_t max_states, uint64_t now_ms) {
    if (!srv || max_states == 0u) {
        return;
    }
    if (!srv->state_cursor || !srv->entity_known_bits || srv->entity_known_stride_bytes == 0u) {
        return;
    }
    if (!srv->clients[client_id].active || !srv->clients[client_id].joined) {
        return;
    }

    const uint16_t total_entities = srv->cfg.max_entities;
    if (total_entities == 0u) {
        return;
    }

    const size_t known_stride = srv->entity_known_stride_bytes;
    const uint8_t *known = srv->entity_known_bits + ((size_t)client_id * known_stride);

    uint16_t cursor = (uint16_t)(srv->state_cursor[client_id] % total_entities);
    uint16_t sent = 0u;
    uint16_t scanned = 0u;

    while (sent < max_states && scanned < total_entities) {
        const uint16_t ei = (uint16_t)((cursor + scanned) % total_entities);
        scanned++;

        if (!srv->entities[ei].active) {
            continue;
        }
        if (!bitset_test(known, ei)) {
            continue;
        }

        struct server_repl_send_job_ctx ctx = {srv, client_id, ei, now_ms};
        send_state_job(&ctx);
        sent++;
    }

    srv->state_cursor[client_id] = (uint16_t)((cursor + scanned) % total_entities);
}

int server_repl_server_tick(server_repl_server_t *srv, uint64_t now_ms) {
    if (!srv) {
        return SERVER_REPL_ERR_INVALID;
    }

    net_repl_spawn_batch_entry_t *spawn_entries = srv->spawn_batch_entries;
    uint16_t *spawn_entry_entity_indices = srv->spawn_batch_entity_indices;
    const uint16_t entry_capacity = srv->spawn_batch_entry_capacity;
    uint8_t spawn_payload[NET_RUDP_MAX_PACKET_SIZE];

    for (uint16_t ci = 0u; ci < srv->cfg.max_clients; ++ci) {
        if (!srv->clients[ci].active || !srv->clients[ci].joined) {
            continue;
        }
        (void)net_rudp_peer_tick_resend(&srv->clients[ci].peer, srv->sock, &srv->clients[ci].addr, now_ms);
    }

    for (uint16_t ci = 0u; ci < srv->cfg.max_clients; ++ci) {
        if (!srv->clients[ci].active || !srv->clients[ci].joined) {
            continue;
        }

        if (!srv->clients[ci].welcome_sent) {
            if (server_repl_try_send_welcome(srv, ci, now_ms) == NET_RUDP_ERR_FULL) {
                continue;
            }
        }

        if (spawn_entries && spawn_entry_entity_indices && entry_capacity > 0u) {
            const uint16_t max_batches_this_tick = 2u;
            (void)server_repl_try_send_spawn_batch(srv,
                                                   ci,
                                                   max_batches_this_tick,
                                                   spawn_entries,
                                                   spawn_entry_entity_indices,
                                                   entry_capacity,
                                                   spawn_payload,
                                                   sizeof(spawn_payload),
                                                   now_ms);
        }

        /* Throttle state updates per-client to avoid send storms at high client counts. */
        server_repl_send_some_states(srv, ci, 2u, now_ms);
    }

    srv->server_tick = (uint16_t)(srv->server_tick + 1u);
    return SERVER_REPL_OK;
}
