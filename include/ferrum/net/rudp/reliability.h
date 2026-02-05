#ifndef FERRUM_NET_RUDP_RELIABILITY_H
#define FERRUM_NET_RUDP_RELIABILITY_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/wire_frame.h"

/** @file
 * @brief Reliability + reassembly layer for RUDP.
 *
 * This layer consumes already-decoded packet headers + wire frame views and:
 * - Retires ACKed reliable send slots.
 * - Applies the receive ACK/duplicate window (reliable packets only).
 * - Optionally reassembles fragmented payloads.
 *
 * It intentionally does not perform wire framing decode/encode.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Process a decoded RUDP frame through reliability + reassembly.
 *
 * Ownership:
 * - `frame_view->payload` is owned by the caller and must remain valid for the
 *   duration of this call only.
 * - `out_payload` is caller-owned storage.
 *
 * Nullability:
 * - All pointers must be non-NULL.
 *
 * Error semantics:
 * - Returns `NET_RUDP_EMPTY` when a packet is dropped (duplicate/out-of-window)
 *   or when a fragmented message is incomplete.
 */
int net_rudp_reliability_receive(net_rudp_peer_t *peer,
                                const net_packet_header_t *header,
                                const net_rudp_wire_frame_view_t *frame_view,
                                uint8_t *out_reliable,
                                uint16_t *out_schema_id,
                                uint8_t *out_payload,
                                size_t out_payload_capacity,
                                size_t *out_payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_RUDP_RELIABILITY_H */
