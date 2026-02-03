#ifndef FERRUM_SERVER_NET_CLIENT_FIBER_H
#define FERRUM_SERVER_NET_CLIENT_FIBER_H

/**
 * \file client_fiber.h
 * \brief Server-side per-client fiber networking interface built on RUDP stream.
 *
 * Ownership: caller owns the fiber handle and must destroy via fr_server_client_fiber_destroy.
 * Nullability: all pointers must be non-null unless stated otherwise.
 * Error semantics: functions return bool for success; false indicates invalid input or resource exhaustion.
 * Side effects: inject functions may push payloads to configured topics.
 */

#include <stdbool.h>
#include <stddef.h>
#include "ferrum/net/topic_channel.h"

/** Public types (max 2): configuration and opaque fiber handle */
typedef struct fr_server_client_fiber_t fr_server_client_fiber_t; /* opaque */

typedef struct fr_server_client_fiber_config_t {
    unsigned reliable_channels;            /* number of reliable channels */
    unsigned reliable_slot_count;          /* per-channel slot buffer size */
    unsigned max_payload_size;             /* maximum payload size */
    fr_topic_channel_t **topics;           /* optional topics array (nullable) */
    unsigned num_topics;                   /* number of topics */
} fr_server_client_fiber_config_t;

/** Create a server-side client fiber. Returns NULL on allocation failure or invalid config. */
fr_server_client_fiber_t *fr_server_client_fiber_create(const fr_server_client_fiber_config_t *cfg);

/** Destroy the fiber and release all resources. Safe to call with NULL (no-op). */
void fr_server_client_fiber_destroy(fr_server_client_fiber_t *fiber);

/** Inject a raw frame into the fiber for processing.
 * Frame format: [seq:u16 LE][chan:u16 LE][payload]
 * Returns true on accept; false if frame invalid (too short, exceeds max payload) or resource error.
 */
bool fr_server_client_fiber_inject_frame(fr_server_client_fiber_t *fiber, const unsigned char *frame, size_t len);

#endif /* FERRUM_SERVER_NET_CLIENT_FIBER_H */
