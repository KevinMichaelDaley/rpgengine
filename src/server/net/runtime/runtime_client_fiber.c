#include <stdint.h>
#include <stdlib.h>
#include <string.h>

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

static int sendto_cb_bridge_(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    fr_server_net_runtime_t *rt = (fr_server_net_runtime_t *)io_user;
    if (!rt || !to || !data) {
        return -1;
    }
    return (sendto_(rt, to, data, size) == NET_UDP_SOCKET_OK) ? 0 : -1;
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

        int rc = reliable ? net_rudp_peer_send_reliable_via(peer,
                                                            rt,
                                                            sendto_cb_bridge_,
                                                            &rt->clients[client_id].addr,
                                                            now_ms,
                                                            schema_id,
                                                            payload,
                                                            payload_size,
                                                            NULL)
                          : net_rudp_peer_send_unreliable_via(peer,
                                                             rt,
                                                             sendto_cb_bridge_,
                                                             &rt->clients[client_id].addr,
                                                             now_ms,
                                                             schema_id,
                                                             payload,
                                                             payload_size);
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
        (void)net_rudp_peer_tick_resend_via(&peer, rt, sendto_cb_bridge_, &client->addr, now_ms);

        job_yield();
    }
}
