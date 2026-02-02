#include <string.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/replication/join.h"
#include "ferrum/net/replication/spawn.h"

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
            net_rudp_peer_init(&srv->clients[i].peer, NET_RUDP_PROTOCOL_ID_P008, srv->cfg.resend_interval_ms);
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
            return;
        }
    }
}

void server_repl_broadcast_spawn(server_repl_server_t *srv, uint16_t new_client_id, uint64_t now_ms) {
    ensure_entity_for_client(srv, new_client_id);

    for (uint16_t ei = 0u; ei < srv->cfg.max_entities; ++ei) {
        if (!srv->entities[ei].active) {
            continue;
        }

        net_repl_spawn_t spawn;
        spawn.entity_id = srv->entities[ei].entity_id;
        spawn.owner_client_id = srv->entities[ei].owner_client_id;
        spawn.join_time_u16 = srv->server_tick;
        spawn.pos_mm = (net_repl_vec3_mm_t){0, 0, 0};

        uint8_t payload[NET_REPL_SPAWN_PAYLOAD_SIZE];
        if (net_repl_spawn_encode(&spawn, payload, sizeof(payload)) != NET_REPL_OK) {
            continue;
        }

        for (uint16_t ci = 0u; ci < srv->cfg.max_clients; ++ci) {
            if (!srv->clients[ci].active || !srv->clients[ci].joined) {
                continue;
            }
            (void)net_rudp_peer_send_reliable(&srv->clients[ci].peer,
                                              srv->sock,
                                              &srv->clients[ci].addr,
                                              now_ms,
                                              NET_REPL_SCHEMA_SPAWN,
                                              payload,
                                              sizeof(payload),
                                              NULL);
            srv->stats.packets_sent++;
            srv->stats.bytes_sent += (uint64_t)(NET_PACKET_HEADER_SIZE + 8u + sizeof(payload));
        }
    }
}

int server_repl_server_pump(server_repl_server_t *srv, uint64_t now_ms) {
    if (!srv) {
        return SERVER_REPL_ERR_INVALID;
    }

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
        uint8_t payload[256];
        size_t payload_size = 0u;
        int prc = net_rudp_peer_receive(&srv->clients[idx].peer,
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
        (void)reliable;

        if (schema_id == NET_REPL_SCHEMA_JOIN) {
            net_repl_join_t join;
            if (net_repl_join_decode(&join, payload, payload_size) != NET_REPL_OK) {
                continue;
            }
            if (!srv->clients[idx].joined) {
                srv->clients[idx].joined = 1u;
                srv->clients[idx].join_nonce = join.client_nonce;
                server_repl_broadcast_spawn(srv, (uint16_t)idx, now_ms);
            }
        }
    }

    return SERVER_REPL_OK;
}
