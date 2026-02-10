#ifndef FERRUM_SERVER_REPL_SERVER_H
#define FERRUM_SERVER_REPL_SERVER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/job/system.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"
#include "ferrum/net/udp_socket.h"

/** @file
 * @brief Multi-client replication server (p008).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define SERVER_REPL_OK 0
#define SERVER_REPL_ERR_INVALID -1

/** Optional callback to fetch an entity's world-space pose for replication.
 *  When set, the repl server calls this instead of its built-in placeholder
 *  motion.  Return true and fill out_pos/out_rot; return false to skip.
 */
typedef bool (*server_repl_get_entity_pose_fn)(void *user,
                                               uint32_t entity_id,
                                               uint16_t entity_index,
                                               vec3_t *out_pos,
                                               quat_t *out_rot);

typedef struct server_repl_config {
    uint16_t max_clients;
    uint16_t tick_hz;
    uint16_t max_entities;
    uint32_t resend_interval_ms;

    /** Optional pose callback.  When non-NULL, replaces the default circular-
     *  motion placeholder with real entity positions (e.g. from physics). */
    server_repl_get_entity_pose_fn get_entity_pose;
    void *get_entity_pose_user;

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
    uint64_t state_jobs_scheduled;
    /** Total nanoseconds spent in network I/O (recv, send, resend). */
    uint64_t net_io_ns_total;
    /** Total nanoseconds spent computing/encoding state updates. */
    uint64_t state_update_ns_total;
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

/** Debug/Testing helpers: not for production use.
 * These functions allow tests to set up server state without real network traffic.
 */
/** Marks a client as active and joined. Returns 0 on success. */
int server_repl_server_debug_force_client_joined(server_repl_server_t *srv, uint16_t client_id);
/** Adds an active entity owned by the given client. Returns 0 on success and writes the entity index. */
int server_repl_server_debug_add_active_entity(server_repl_server_t *srv, uint16_t owner_client_id, uint32_t entity_id_hint, uint16_t *out_index);
/** Marks an entity index as known to a client (enables state updates). Returns 0 on success. */
int server_repl_server_debug_force_entity_known(server_repl_server_t *srv, uint16_t client_id, uint16_t entity_index);
/** Schedules a single state-send job for a client/entity using the job system. Returns 0 on success, -1 on invalid or full queue. */
int server_repl_server_debug_schedule_state_job(server_repl_server_t *srv, uint16_t client_id, uint16_t entity_index, uint64_t now_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_REPL_SERVER_H */
