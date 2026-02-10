#ifndef FERRUM_SERVER_PHYSICS_NET_BODY_STATE_BROADCAST_H
#define FERRUM_SERVER_PHYSICS_NET_BODY_STATE_BROADCAST_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/net/topic_channel.h"
#include "ferrum/physics/world.h"

/** @file
 * @brief Server-side broadcaster for NET_REPL_SCHEMA_BODY_STATE (unreliable).
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Configuration for the body-state broadcaster.
 *
 * Each tick, this module scans active physics bodies and enqueues
 * NET_REPL_SCHEMA_BODY_STATE messages to connected clients' outbound
 * unreliable topics.
 */
typedef struct fr_server_body_state_broadcast_config_t {
    /** Maximum number of clients (>=1). */
    uint16_t max_clients;

    /** Physics world to read body state from (non-NULL). */
    phys_world_t *world;

    /** Callback to fetch per-client outbound topics.
     * Must return true and fill both pointers for connected clients.
     */
    bool (*get_client_out_topics_cb)(void *user,
                                     uint16_t client_id,
                                     fr_topic_channel_t **out_reliable,
                                     fr_topic_channel_t **out_unreliable);

    /** User pointer passed to callbacks. */
    void *io_user;
} fr_server_body_state_broadcast_config_t;

/** Opaque broadcaster handle. */
typedef struct fr_server_body_state_broadcast fr_server_body_state_broadcast_t;

/** Create a broadcaster. Returns NULL on invalid args or allocation failure. */
fr_server_body_state_broadcast_t *fr_server_body_state_broadcast_create(const fr_server_body_state_broadcast_config_t *cfg);

/** Destroy a broadcaster. Safe to call with NULL. */
void fr_server_body_state_broadcast_destroy(fr_server_body_state_broadcast_t *b);

/** Enqueue per-body state updates for this tick.
 *
 * @param b Broadcaster.
 * @param server_tick Server tick counter (encoded into BODY_STATE).
 * @param now_ms Wall clock time in ms (truncated to u32 in BODY_STATE).
 * @return false on invalid args.
 */
bool fr_server_body_state_broadcast_tick(fr_server_body_state_broadcast_t *b,
                                        uint16_t server_tick,
                                        uint64_t now_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_PHYSICS_NET_BODY_STATE_BROADCAST_H */
