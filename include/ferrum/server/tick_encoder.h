/**
 * @file tick_encoder.h
 * @brief Server tick encoder: per-client event + state encoding
 *        feeding outbound topic channels.
 *
 * Each tick, the encoder iterates active clients and invokes
 * pluggable callbacks to encode events (→ reliable topic) and
 * body/state data (→ unreliable topic).
 *
 * Types: fr_server_tick_encoder_config_t, fr_server_tick_encoder_t.
 */
#ifndef FERRUM_SERVER_TICK_ENCODER_H
#define FERRUM_SERVER_TICK_ENCODER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/net/topic_channel.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback to obtain per-client outbound topics.
 *
 * Returns true if client_id is active and fills both topic pointers.
 */
typedef bool (*fr_server_encoder_get_topics_fn)(
    void *user,
    uint16_t client_id,
    fr_topic_channel_t **out_reliable,
    fr_topic_channel_t **out_unreliable);

/**
 * @brief Per-client encoder callback (events or state).
 *
 * Writes encoded messages into the provided topic channel.
 *
 * @param user       Opaque encoder context.
 * @param client_id  Client being encoded for.
 * @param topic      Outbound topic to push into.
 * @param tick       Current server tick.
 * @return 0 on success, non-zero on error.
 */
typedef int (*fr_server_encode_fn)(void *user,
                                   uint16_t client_id,
                                   fr_topic_channel_t *topic,
                                   uint64_t tick);

/**
 * @brief Configuration for the tick encoder.
 */
typedef struct fr_server_tick_encoder_config {
    /** Maximum client count (must be > 0). */
    uint16_t max_clients;

    /** Callback to obtain per-client outbound topics (required). */
    fr_server_encoder_get_topics_fn get_client_out_topics;

    /** User pointer for get_client_out_topics callback. */
    void *io_user;

    /** Event encoder callback (reliable channel). Optional. */
    fr_server_encode_fn encode_events;

    /** State encoder callback (unreliable channel). Optional. */
    fr_server_encode_fn encode_state;

    /** User pointer for encode callbacks. */
    void *encode_user;
} fr_server_tick_encoder_config_t;

/**
 * @brief Tick encoder state. Stack-allocatable.
 */
typedef struct fr_server_tick_encoder {
    uint16_t max_clients;
    fr_server_encoder_get_topics_fn get_topics;
    void *io_user;
    fr_server_encode_fn encode_events;
    fr_server_encode_fn encode_state;
    void *encode_user;
} fr_server_tick_encoder_t;

/* ── API ───────────────────────────────────────────────────────── */

/**
 * @brief Initialize a tick encoder.
 *
 * @param enc  Encoder to initialize (non-NULL).
 * @param cfg  Configuration (non-NULL, max_clients > 0, get_topics required).
 * @return 0 on success, -1 on invalid args.
 */
int fr_server_tick_encoder_init(fr_server_tick_encoder_t *enc,
                                const fr_server_tick_encoder_config_t *cfg);

/**
 * @brief Run encoding for one tick.
 *
 * Iterates all clients, fetches their topics, and invokes event/state
 * encoder callbacks.
 *
 * @param enc   Initialized encoder (non-NULL).
 * @param tick  Current server tick.
 * @return 0 on success, -1 on invalid args.
 */
int fr_server_tick_encoder_run(fr_server_tick_encoder_t *enc,
                               uint64_t tick);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SERVER_TICK_ENCODER_H */
