#ifndef FERRUM_SERVER_REPL_INTERNAL_H
#define FERRUM_SERVER_REPL_INTERNAL_H

#include <stdint.h>

#include "ferrum/job/system.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/server/repl_server.h"

struct server_repl_client {
    net_udp_addr_t addr;
    net_rudp_peer_t peer;
    uint32_t join_nonce;
    uint8_t active;
    uint8_t joined;
};

struct server_repl_entity {
    uint32_t entity_id;
    uint16_t owner_client_id;
    uint8_t active;
};

struct server_repl_server {
    server_repl_config_t cfg;
    net_udp_socket_t *sock;
    job_system_t *jobs;

    struct server_repl_client *clients;
    struct server_repl_entity *entities;

    uint16_t server_tick;
    uint32_t next_entity_id;

    server_repl_stats_t stats;
};

int server_repl_find_or_add_client(server_repl_server_t *srv, const net_udp_addr_t *addr);
void server_repl_broadcast_spawn(server_repl_server_t *srv, uint16_t new_client_id, uint64_t now_ms);

#endif /* FERRUM_SERVER_REPL_INTERNAL_H */
