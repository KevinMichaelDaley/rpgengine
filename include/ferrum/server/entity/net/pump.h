#ifndef FERRUM_SERVER_ENTITY_NET_PUMP_H
#define FERRUM_SERVER_ENTITY_NET_PUMP_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/topic_channel.h"

/** \file
 * \brief Server entity/player networking pump.
 *
 * Consumes decoded inbound messages (from the per-client networking runtime)
 * and produces:
 * - high-level player/entity events to event topics
 * - outbound replication messages enqueued onto per-client outbound topics
 *
 * This module does not touch sockets and does not parse protocol frames.
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Event codes for `player_event_topic` / `entity_event_topic`.
 * Kept as macros (not an enum) to preserve the 2-type rule for public headers.
 */
#define FR_SERVER_EVT_PLAYER_JOIN  1u
#define FR_SERVER_EVT_PLAYER_SPAWN 2u
#define FR_SERVER_EVT_ENTITY_JOIN  3u
#define FR_SERVER_EVT_ENTITY_SPAWN 4u

/** Configuration for the server entity net pump.
 *
 * Topic conventions:
 * - Inbound messages are produced by `fr_server_net_runtime`.
 * - Outbound messages are written to per-client outbound topics using:
 *   - reliable topic: [schema_id:u16 LE][payload...]
 *   - unreliable topic: [schema_id:u16 LE][payload...]
 */
typedef struct fr_server_entity_net_pump_config_t {
    uint16_t max_clients;

    /** Server tick rate used in outbound WELCOME. */
    uint16_t tick_hz;

    /** Expected number of spawned entities advertised in WELCOME. */
    uint16_t expected_entities;

    /** Inbound decoded messages from the network runtime. */
    fr_topic_channel_t *inbound_topic;

    /** High-level player events (EVT_PLAYER_*). */
    fr_topic_channel_t *player_event_topic;

    /** High-level non-player entity events (EVT_ENTITY_*). */
    fr_topic_channel_t *entity_event_topic;

    /** Callback to fetch per-client outbound topics.
     * Must return true and fill both pointers for connected clients.
     */
    bool (*get_client_out_topics_cb)(void *user,
                                    uint16_t client_id,
                                    fr_topic_channel_t **out_reliable,
                                    fr_topic_channel_t **out_unreliable);

    /** User pointer passed to callbacks. */
    void *io_user;
} fr_server_entity_net_pump_config_t;

/** Opaque pump handle. */
typedef struct fr_server_entity_net_pump fr_server_entity_net_pump_t;

/** Create the pump. Returns NULL on invalid args or allocation failure. */
fr_server_entity_net_pump_t *fr_server_entity_net_pump_create(const fr_server_entity_net_pump_config_t *cfg);

/** Destroy the pump. Safe to call with NULL. */
void fr_server_entity_net_pump_destroy(fr_server_entity_net_pump_t *pump);

/** Consume inbound messages and enqueue outputs.
 * Returns false on invalid args.
 */
bool fr_server_entity_net_pump_tick(fr_server_entity_net_pump_t *pump, uint64_t now_ms);

/** Update whether a player's in-world representation should be spawned to remote clients.
 * Returns false if client_id is invalid or not yet joined.
 */
bool fr_server_entity_net_pump_set_player_should_spawn_remote(fr_server_entity_net_pump_t *pump,
                                                              uint16_t client_id,
                                                              bool should_spawn_remote);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_SERVER_ENTITY_NET_PUMP_H */
