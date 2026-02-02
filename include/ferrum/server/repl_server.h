#ifndef FERRUM_SERVER_REPL_SERVER_H
#define FERRUM_SERVER_REPL_SERVER_H

#include <stddef.h>
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

    /** Optional caller-provided storage for internal client state array.
     * If provided, must be at least `server_repl_client_storage_size(max_clients)` bytes.
     * If NULL, the module will allocate internally.
     */
    void *client_storage;
    size_t client_storage_bytes;

    /** Optional caller-provided storage for internal entity state array.
     * If provided, must be at least `server_repl_entity_storage_size(max_entities)` bytes.
     * If NULL, the module will allocate internally.
     */
    void *entity_storage;
    size_t entity_storage_bytes;

    /** Optional caller-provided scratch storage for per-tick send job contexts.
     * If provided, must be at least `server_repl_send_job_ctx_storage_size(max_clients, max_entities)` bytes.
     * If NULL, the module will allocate per-tick (slower).
     */
    void *send_job_ctx_storage;
    size_t send_job_ctx_storage_bytes;

    /** Caller-provided RUDP reliable send-slot storage for ALL clients, laid out as:
     * `max_clients * rudp_send_slots_per_client` contiguous `net_rudp_send_slot_t`.
     * If NULL, the module will fall back to a fixed default sizing.
     */
    void *rudp_send_slot_storage;
    size_t rudp_send_slot_storage_bytes;
    size_t rudp_send_slots_per_client;
} server_repl_config_t;

typedef struct server_repl_stats {
    uint32_t clients_connected;
    uint64_t packets_sent;
    uint64_t packets_recv;
    uint64_t bytes_sent;
    uint64_t bytes_recv;
} server_repl_stats_t;

typedef struct server_repl_server server_repl_server_t;

/** Returns the required size (bytes) for `server_repl_config_t::client_storage`. */
size_t server_repl_client_storage_size(uint16_t max_clients);

/** Returns the required size (bytes) for `server_repl_config_t::entity_storage`. */
size_t server_repl_entity_storage_size(uint16_t max_entities);

/** Returns the required size (bytes) for `server_repl_config_t::send_job_ctx_storage`. */
size_t server_repl_send_job_ctx_storage_size(uint16_t max_clients, uint16_t max_entities);

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
