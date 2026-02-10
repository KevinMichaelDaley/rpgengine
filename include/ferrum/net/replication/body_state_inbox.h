/**
 * @file
 * @brief Newest-wins inbox for unreliable BODY_STATE datagrams.
 *
 * This module applies unreliable body state updates with "newest-wins" semantics:
 * for each body_id, only updates with a strictly newer server_tick are accepted.
 * Older (out-of-order) datagrams are ignored.
 */
#ifndef FERRUM_NET_REPLICATION_BODY_STATE_INBOX_H
#define FERRUM_NET_REPLICATION_BODY_STATE_INBOX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/body_state.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Newest-wins storage for BODY_STATE messages. */
typedef struct fr_body_state_inbox {
    uint16_t max_bodies;
    net_repl_body_state_t *states;
    uint16_t *last_server_tick;
    uint8_t *has_state;
} fr_body_state_inbox_t;

/** Initialize an inbox. Allocates internal storage; returns false on error. */
bool fr_body_state_inbox_init(fr_body_state_inbox_t *inbox, uint16_t max_bodies);

/** Destroy an inbox and free internal storage (NULL-safe). */
void fr_body_state_inbox_destroy(fr_body_state_inbox_t *inbox);

/**
 * Push an encoded BODY_STATE payload.
 *
 * @return true if the update was accepted/applied, false if dropped or invalid.
 */
bool fr_body_state_inbox_push(fr_body_state_inbox_t *inbox, const uint8_t *payload, size_t payload_size);

/**
 * Get the latest applied state for a body.
 * @return true if available.
 */
bool fr_body_state_inbox_get(const fr_body_state_inbox_t *inbox, uint16_t body_id, net_repl_body_state_t *out_state);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_BODY_STATE_INBOX_H */
