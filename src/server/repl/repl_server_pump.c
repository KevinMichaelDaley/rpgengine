#include <stddef.h>
#include <string.h>
#include <time.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/replication/join.h"
#include "internal.h"

static int addr_equal(const net_udp_addr_t *a, const net_udp_addr_t *b) {
    if (!a || !b) {
        return 0;
    }
    if (a->len != b->len) {
        return 0;
    }
    return memcmp(a->storage, b->storage, a->len) == 0;
}

int server_repl_find_or_add_client(server_repl_server_t *srv, const net_udp_addr_t *addr) {
    for (uint16_t i = 0u; i < srv->cfg.max_clients; ++i) {
        if (srv->clients[i].active && addr_equal(&srv->clients[i].addr, addr)) {
            return (int)i;
        }
    }
    for (uint16_t i = 0u; i < srv->cfg.max_clients; ++i) {
        if (!srv->clients[i].active) {
            srv->clients[i].active = 1u;
            srv->clients[i].joined = 0u;
            srv->clients[i].join_nonce = 0u;
            srv->clients[i].addr = *addr;

            net_rudp_send_slot_t *slots = (net_rudp_send_slot_t *)srv->cfg.rudp_send_slot_storage;
            slots += (size_t)i * (size_t)srv->cfg.rudp_send_slots_per_client;
            net_rudp_peer_init_with_storage(&srv->clients[i].peer,
                                            NET_RUDP_PROTOCOL_ID_P008,
                                            srv->cfg.resend_interval_ms,
                                            slots,
                                            srv->cfg.rudp_send_slots_per_client);
            srv->stats.clients_connected++;
            return (int)i;
        }
    }
    return -1;
}

static void ensure_entity_for_client(server_repl_server_t *srv, uint16_t client_id) {
    for (uint16_t i = 0u; i < srv->cfg.max_entities; ++i) {
        if (srv->entities[i].active && srv->entities[i].owner_client_id == client_id) {
            return;
        }
    }
    for (uint16_t i = 0u; i < srv->cfg.max_entities; ++i) {
        if (!srv->entities[i].active) {
            srv->entities[i].active = 1u;
            srv->entities[i].owner_client_id = client_id;
            srv->entities[i].entity_id = srv->next_entity_id++;

            if (srv->entity_known_bits && srv->entity_known_stride_bytes > 0u) {
                const size_t byte_index = (size_t)i >> 3u;
                const uint8_t mask = (uint8_t)(1u << (i & 7u));
                for (uint16_t ci = 0u; ci < srv->cfg.max_clients; ++ci) {
                    uint8_t *known = srv->entity_known_bits + ((size_t)ci * srv->entity_known_stride_bytes);
                    known[byte_index] = (uint8_t)(known[byte_index] & (uint8_t)~mask);
                }
            }
            return;
        }
    }
}

int server_repl_server_pump(server_repl_server_t *srv, uint64_t now_ms) {
    if (!srv) {
        return SERVER_REPL_ERR_INVALID;
    }
    (void)now_ms;

    /* Measure time spent in network receive + decode path. */
    struct timespec ts0, ts1;
    clock_gettime(CLOCK_MONOTONIC, &ts0);

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    for (;;) {
        net_udp_addr_t from;
        size_t packet_size = 0u;
        int rc = net_udp_socket_recvfrom(srv->sock, &from, packet, sizeof(packet), &packet_size);
        if (rc == NET_UDP_SOCKET_EMPTY) {
            break;
        }
        if (rc != NET_UDP_SOCKET_OK) {
            break;
        }
        srv->stats.packets_recv++;
        srv->stats.bytes_recv += packet_size;

        int idx = server_repl_find_or_add_client(srv, &from);
        if (idx < 0) {
            continue;
        }

        uint8_t reliable = 0u;
        uint16_t schema_id = 0u;
        uint8_t payload[NET_RUDP_MAX_PACKET_SIZE];
        size_t payload_size = 0u;
        int prc = net_rudp_peer_receive(&srv->clients[idx].peer,
                                        packet,
                                        packet_size,
                                        now_ms,
                                        &reliable,
                                        &schema_id,
                                        payload,
                                        sizeof(payload),
                                        &payload_size);
        if (prc != NET_RUDP_OK) {
            continue;
        }
        (void)reliable;

        if (schema_id == NET_REPL_SCHEMA_JOIN) {
            net_repl_join_t join;
            if (net_repl_join_decode(&join, payload, payload_size) != NET_REPL_OK) {
                continue;
            }
            if (!srv->clients[idx].joined) {
                srv->clients[idx].joined = 1u;
                srv->clients[idx].join_nonce = join.client_nonce;
                ensure_entity_for_client(srv, (uint16_t)idx);

                srv->clients[idx].welcome_sent = 0u;
                if (srv->entity_known_bits && srv->entity_known_stride_bytes > 0u) {
                    memset(srv->entity_known_bits + ((size_t)idx * srv->entity_known_stride_bytes),
                           0,
                           srv->entity_known_stride_bytes);
                }
                if (srv->spawn_cursor) {
                    srv->spawn_cursor[idx] = 0u;
                }
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &ts1);
    uint64_t d_ns = (uint64_t)(ts1.tv_sec - ts0.tv_sec) * 1000000000ull + (uint64_t)(ts1.tv_nsec - ts0.tv_nsec);
    srv->stats.net_io_ns_total += d_ns;
    return SERVER_REPL_OK;
}
