#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "ferrum/net/packet_header.h"

#include "runtime_internal.h"

#define OUT_MSG_MAX (2u + NET_RUDP_MAX_PACKET_SIZE)

static int sendto_(fr_server_net_runtime_t *rt, const net_udp_addr_t *to, const void *data, size_t size) {
    if (!rt || !to || !data) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    if (rt->cfg.sendto_cb) {
        return rt->cfg.sendto_cb(rt->cfg.io_user, to, data, size);
    }
    return net_udp_socket_sendto(rt->cfg.socket, to, data, size);
}

static void write_u16_be_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xFFu);
    out[1] = (uint8_t)(v & 0xFFu);
}

static int build_packet_(net_rudp_peer_t *peer,
                         uint64_t now_ms,
                         uint8_t reliable,
                         uint16_t schema_id,
                         const void *payload,
                         size_t payload_size,
                         uint8_t *out_packet,
                         size_t out_capacity,
                         size_t *out_size,
                         uint16_t *out_sequence) {
    if (!peer || !payload || !out_packet || !out_size) {
        return NET_RUDP_ERR_INVALID;
    }
    (void)now_ms;

    if (payload_size > (NET_RUDP_MAX_PACKET_SIZE - NET_PACKET_HEADER_SIZE - 8u)) {
        return NET_RUDP_ERR_INVALID;
    }
    if (out_capacity < NET_PACKET_HEADER_SIZE + 8u + payload_size) {
        return NET_RUDP_ERR_SHORT;
    }

    net_packet_header_t header;
    header.protocol_id = peer->protocol_id;
    header.sequence = peer->next_sequence;
    header.ack = net_ack_window_ack(&peer->recv_window);
    header.ack_bits = net_ack_window_ack_bits(&peer->recv_window);

    int rc = net_packet_header_encode(&header, out_packet, out_capacity);
    if (rc != NET_PACKET_HEADER_OK) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    uint8_t *frame = out_packet + NET_PACKET_HEADER_SIZE;
    frame[0] = reliable ? 0x01u : 0u;
    frame[1] = 0u;
    write_u16_be_(frame + 2, schema_id);
    write_u16_be_(frame + 4, (uint16_t)payload_size);
    write_u16_be_(frame + 6, 0u);
    memcpy(frame + 8u, payload, payload_size);

    *out_size = NET_PACKET_HEADER_SIZE + 8u + payload_size;
    if (out_sequence) {
        *out_sequence = header.sequence;
    }
    peer->next_sequence = (uint16_t)(peer->next_sequence + 1u);
    return NET_RUDP_OK;
}

static int send_unreliable_(fr_server_net_runtime_t *rt,
                            net_rudp_peer_t *peer,
                            const net_udp_addr_t *to,
                            uint64_t now_ms,
                            uint16_t schema_id,
                            const void *payload,
                            size_t payload_size) {
    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    size_t packet_size = 0u;
    int rc = build_packet_(peer, now_ms, 0u, schema_id, payload, payload_size, packet, sizeof(packet), &packet_size, NULL);
    if (rc != NET_RUDP_OK) {
        return rc;
    }
    int src = sendto_(rt, to, packet, packet_size);
    return (src == NET_UDP_SOCKET_OK) ? NET_RUDP_OK : NET_RUDP_ERR_PROTOCOL;
}

static int send_reliable_(fr_server_net_runtime_t *rt,
                          net_rudp_peer_t *peer,
                          const net_udp_addr_t *to,
                          uint64_t now_ms,
                          uint16_t schema_id,
                          const void *payload,
                          size_t payload_size) {
    if (!peer->send_slots || peer->send_slot_count == 0u) {
        return NET_RUDP_ERR_FULL;
    }

    size_t slot = peer->send_slot_count;
    for (size_t i = 0u; i < peer->send_slot_count; ++i) {
        if (!peer->send_slots[i].used) {
            slot = i;
            break;
        }
    }
    if (slot == peer->send_slot_count) {
        return NET_RUDP_ERR_FULL;
    }

    size_t packet_size = 0u;
    uint16_t seq = 0u;
    int rc = build_packet_(peer,
                           now_ms,
                           1u,
                           schema_id,
                           payload,
                           payload_size,
                           peer->send_slots[slot].packet_bytes,
                           NET_RUDP_MAX_PACKET_SIZE,
                           &packet_size,
                           &seq);
    if (rc != NET_RUDP_OK) {
        return rc;
    }

    peer->send_slots[slot].used = 1u;
    peer->send_slots[slot].sequence = seq;
    peer->send_slots[slot].size = (uint16_t)packet_size;
    peer->send_slots[slot].last_send_ms = now_ms;

    int src = sendto_(rt, to, peer->send_slots[slot].packet_bytes, packet_size);
    return (src == NET_UDP_SOCKET_OK) ? NET_RUDP_OK : NET_RUDP_ERR_PROTOCOL;
}

static void tick_resend_(fr_server_net_runtime_t *rt,
                         net_rudp_peer_t *peer,
                         const net_udp_addr_t *to,
                         uint64_t now_ms) {
    if (!peer->send_slots || peer->send_slot_count == 0u) {
        return;
    }
    for (size_t i = 0u; i < peer->send_slot_count; ++i) {
        if (!peer->send_slots[i].used) {
            continue;
        }
        uint64_t elapsed = now_ms - peer->send_slots[i].last_send_ms;
        if (elapsed < (uint64_t)peer->resend_interval_ms) {
            continue;
        }
        peer->send_slots[i].last_send_ms = now_ms;
        (void)sendto_(rt, to, peer->send_slots[i].packet_bytes, (size_t)peer->send_slots[i].size);
    }
}

static void publish_inbound_(fr_server_net_runtime_t *rt,
                             uint16_t client_id,
                             uint8_t reliable,
                             uint16_t schema_id,
                             const uint8_t *payload,
                             size_t payload_size) {
    uint8_t msg[6u + NET_RUDP_MAX_PACKET_SIZE];
    if (!rt || !rt->cfg.inbound_topic || payload_size > NET_RUDP_MAX_PACKET_SIZE) {
        return;
    }

    msg[0] = (uint8_t)(client_id & 0xFFu);
    msg[1] = (uint8_t)((client_id >> 8u) & 0xFFu);
    msg[2] = (uint8_t)(schema_id & 0xFFu);
    msg[3] = (uint8_t)((schema_id >> 8u) & 0xFFu);
    msg[4] = (uint8_t)((reliable ? 1u : 0u) & 0x1u);
    msg[5] = 0u;
    if (payload_size > 0u) {
        memcpy(msg + 6u, payload, payload_size);
    }

    (void)fr_topic_channel_push(rt->cfg.inbound_topic, msg, 6u + payload_size);
}

static void pump_outbound_topic_(fr_server_net_runtime_t *rt,
                                 uint16_t client_id,
                                 fr_topic_channel_t *topic,
                                 net_rudp_peer_t *peer,
                                 uint8_t reliable,
                                 uint64_t now_ms) {
    uint8_t msg[OUT_MSG_MAX];
    for (;;) {
        size_t len = sizeof(msg);
        if (!fr_topic_channel_pop(topic, msg, &len)) {
            break;
        }
        if (len < 2u) {
            continue;
        }
        uint16_t schema_id = (uint16_t)msg[0] | ((uint16_t)msg[1] << 8u);
        const uint8_t *payload = msg + 2u;
        size_t payload_size = len - 2u;

        int rc = reliable ? send_reliable_(rt, peer, &rt->clients[client_id].addr, now_ms, schema_id, payload, payload_size)
                          : send_unreliable_(rt, peer, &rt->clients[client_id].addr, now_ms, schema_id, payload, payload_size);
        if (rc == NET_RUDP_OK) {
            atomic_fetch_add_explicit(&rt->packets_out, 1u, memory_order_relaxed);
            atomic_fetch_add_explicit(&rt->bytes_out, (uint64_t)payload_size, memory_order_relaxed);
        }
    }
}

void fr_server_client_fiber_main(void *user) {
    fr_server_client_fiber_args_t *args = (fr_server_client_fiber_args_t *)user;
    if (!args || !args->rt) {
        free(args);
        return;
    }
    fr_server_net_runtime_t *rt = args->rt;
    const uint16_t client_id = args->client_id;
    free(args);

    if (client_id >= rt->cfg.max_clients) {
        return;
    }
    fr_server_client_t *client = &rt->clients[client_id];

    fr_server_client_inbox_t inbox;
    memset(&inbox, 0, sizeof(inbox));
    atomic_init(&inbox.read_idx, 0u);
    atomic_init(&inbox.write_idx, 0u);
    atomic_store_explicit(&client->inbox_ptr, (uintptr_t)&inbox, memory_order_release);

    if (atomic_load_explicit(&client->pending_used, memory_order_acquire)) {
        size_t staged = (size_t)client->pending_size;
        if (staged > 0u && staged <= NET_RUDP_MAX_PACKET_SIZE) {
            (void)fr_server_client_inbox_try_push(&inbox, client->pending_packet, staged);
        }
        atomic_store_explicit(&client->pending_used, false, memory_order_release);
        client->pending_size = 0u;
    }

    /* Stack-owned reliable resend slots (kept small to fit typical fiber stacks). */
    net_rudp_send_slot_t send_slots[16u];
    memset(send_slots, 0, sizeof(send_slots));
    net_rudp_peer_t peer;
    net_rudp_peer_init_with_storage(&peer, NET_RUDP_PROTOCOL_ID_P008, 50u, send_slots, (size_t)(sizeof(send_slots) / sizeof(send_slots[0])));

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];

    for (;;) {
        if (atomic_load_explicit(&client->stop, memory_order_acquire)) {
            break;
        }
        if (atomic_load_explicit(&rt->cfg.jobs->shutting_down, memory_order_acquire)) {
            break;
        }

        uint64_t now_ms = atomic_load_explicit(&client->now_ms, memory_order_acquire);

        /* Inbound packets */
        for (;;) {
            size_t packet_size = 0u;
            if (!fr_server_client_inbox_try_pop(&inbox, packet, sizeof(packet), &packet_size)) {
                break;
            }
            uint8_t reliable = 0u;
            uint16_t schema_id = 0u;
            size_t payload_size = 0u;
            int prc = net_rudp_peer_receive(&peer,
                                            packet,
                                            packet_size,
                                            &reliable,
                                            &schema_id,
                                            payload,
                                            sizeof(payload),
                                            &payload_size);
            if (prc != NET_RUDP_OK) {
                continue;
            }
            publish_inbound_(rt, client_id, reliable, schema_id, payload, payload_size);
        }

        /* Outbound topics */
        pump_outbound_topic_(rt, client_id, client->out_reliable, &peer, 1u, now_ms);
        pump_outbound_topic_(rt, client_id, client->out_unreliable, &peer, 0u, now_ms);

        /* Reliable resend */
        tick_resend_(rt, &peer, &client->addr, now_ms);

        job_yield();
    }
}
