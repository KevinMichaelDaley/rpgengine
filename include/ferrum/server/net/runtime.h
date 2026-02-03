#ifndef FERRUM_SERVER_NET_RUNTIME_H
#define FERRUM_SERVER_NET_RUNTIME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/topic_channel.h"
#include "ferrum/net/udp_socket.h"
#include "ferrum/job/system.h"

/** \file
 * \brief Server network runtime: per-client fiber jobs with reliable+unreliable streams.
 *
 * Design:
 * - A single demux pump receives UDP datagrams and routes them to per-client fibers.
 * - Each client fiber owns its RUDP peer state and an unreliable packet buffer on its fiber stack.
 * - Fibers publish decoded inbound messages to a global inbound topic.
 * - Fibers poll per-client outbound topics (reliable and unreliable) to send updates.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration for the server net runtime. */
typedef struct fr_server_net_runtime_config_t {
    /** Maximum number of concurrently tracked clients (>=1). */
    uint16_t max_clients;

    /** Job system used to schedule client fibers.
     * When NULL, the runtime creates an internal deterministic job system.
     */
    job_system_t *jobs;

    /** UDP socket used for real networking when callbacks are NULL. */
    net_udp_socket_t *socket;

    /** Global inbound topic for decoded messages (non-NULL).
     *
     * Message format (bytes):
     * - client_id: u16 LE
     * - schema_id: u16 LE
     * - flags: u8 (bit0=reliable)
     * - reserved: u8
     * - payload: schema-specific bytes
     */
    fr_topic_channel_t *inbound_topic;

    /** Optional recvfrom override for tests. Signature matches net_udp_socket_recvfrom behavior. */
    int (*recvfrom_cb)(void *user,
                       net_udp_addr_t *out_from,
                       uint8_t *out_data,
                       size_t out_cap,
                       size_t *out_size);

    /** Optional sendto override for tests. Signature matches net_udp_socket_sendto behavior. */
    int (*sendto_cb)(void *user, const net_udp_addr_t *to, const void *data, size_t size);

    /** User pointer passed to callbacks. */
    void *io_user;
} fr_server_net_runtime_config_t;

/** Opaque runtime handle. */
typedef struct fr_server_net_runtime fr_server_net_runtime_t;

/** Create a server net runtime.
 * Returns NULL on invalid args or allocation failure.
 */
fr_server_net_runtime_t *fr_server_net_runtime_create(const fr_server_net_runtime_config_t *cfg);

/** Destroy a server net runtime and free owned resources.
 * Safe to call with NULL.
 */
void fr_server_net_runtime_destroy(fr_server_net_runtime_t *rt);

/** Pump the demux receive loop once (nonblocking).
 * Returns false on invalid args.
 */
bool fr_server_net_runtime_pump(fr_server_net_runtime_t *rt, uint64_t now_ms);

/** Run fibers in deterministic mode until idle or `max_spins` exhausted.
 * Intended for tests; returns false on invalid args.
 */
bool fr_server_net_runtime_run_fibers(fr_server_net_runtime_t *rt, uint32_t max_spins);

/** Obtain per-client outbound topics.
 * Returns false if client_id is invalid or not allocated yet.
 */
bool fr_server_net_runtime_client_out_topics(fr_server_net_runtime_t *rt,
                                             uint16_t client_id,
                                             fr_topic_channel_t **out_reliable,
                                             fr_topic_channel_t **out_unreliable);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_NET_RUNTIME_H */
