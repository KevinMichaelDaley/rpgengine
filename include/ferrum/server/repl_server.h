#ifndef FERRUM_SERVER_REPL_SERVER_H
#define FERRUM_SERVER_REPL_SERVER_H

#include <stdint.h>

#include "ferrum/job/system.h"
#include "ferrum/net/udp_socket.h"

/** @file
 * @brief Multi-client replication server (p008).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_REPL_OK 0
#define SERVER_REPL_ERR_INVALID -1

typedef struct server_repl_config {
    uint16_t max_clients;
    uint16_t tick_hz;
    uint16_t max_entities;
    uint32_t resend_interval_ms;
} server_repl_config_t;

typedef struct server_repl_stats {
    uint32_t clients_connected;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
} server_repl_stats_t;

typedef struct server_repl_server server_repl_server_t;

server_repl_server_t *server_repl_server_create(const server_repl_config_t *cfg,
                                                net_udp_socket_t *sock,
                                                job_system_t *jobs);

void server_repl_server_destroy(server_repl_server_t *srv);

int server_repl_server_pump(server_repl_server_t *srv, uint64_t now_ms);

int server_repl_server_tick(server_repl_server_t *srv, uint64_t now_ms);

server_repl_stats_t server_repl_server_stats(const server_repl_server_t *srv);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_REPL_SERVER_H */
