#ifndef FERRUM_NET_RUDP_WIRE_FRAME_H
#define FERRUM_NET_RUDP_WIRE_FRAME_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/packet_header.h"

/** @file
 * @brief Wire-level encode/decode helpers for a single RUDP packet.
 *
 * This module is intentionally minimal: it only understands the packet header
 * (protocol id + ack header) and the per-packet frame header (flags + schema/topic id
 * + payload length) to provide safe bounds-checked access to the payload bytes.
 *
 * Reliability, retransmission, and reassembly live above this layer.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_RUDP_WIRE_OK 0
#define NET_RUDP_WIRE_ERR_INVALID -1
#define NET_RUDP_WIRE_ERR_SHORT -2
#define NET_RUDP_WIRE_ERR_PROTOCOL -3

/** Fixed size in bytes of the RUDP per-packet frame header. */
#define NET_RUDP_WIRE_FRAME_HEADER_SIZE 8u

/** Frame flag: payload is reliable (sequence participates in ACK window). */
#define NET_RUDP_WIRE_FLAG_RELIABLE 0x01u
/** Frame flag: payload is a fragmentation chunk (reassembly handled above wire framing). */
#define NET_RUDP_WIRE_FLAG_FRAGMENT 0x02u

/**
 * @brief View of a decoded RUDP frame.
 *
 * Ownership: `payload` points into the caller-provided packet buffer.
 */
typedef struct net_rudp_wire_frame_view {
    uint8_t flags;
    uint16_t schema_id;
    const uint8_t *payload;
    size_t payload_size;
} net_rudp_wire_frame_view_t;

/**
 * @brief Decode a packet into header + frame view.
 *
 * @param out_header Output header (non-NULL).
 * @param out_frame Output frame view (non-NULL).
 * @param packet Packet bytes (non-NULL).
 * @param packet_size Packet byte length.
 * @return NET_RUDP_WIRE_OK on success, or an error code.
 */
int net_rudp_wire_decode(net_packet_header_t *out_header,
                         net_rudp_wire_frame_view_t *out_frame,
                         const uint8_t *packet,
                         size_t packet_size);

/**
 * @brief Encode a packet from a header + frame fields.
 *
 * @param header Header values in host byte order (non-NULL).
 * @param flags Frame flags.
 * @param schema_id Schema/topic id.
 * @param payload Payload bytes (non-NULL when payload_size > 0).
 * @param payload_size Payload byte length.
 * @param out_packet Destination buffer.
 * @param out_capacity Destination buffer capacity in bytes.
 * @param out_size Output: total packet size in bytes.
 * @return NET_RUDP_WIRE_OK on success, or an error code.
 */
int net_rudp_wire_encode(const net_packet_header_t *header,
                         uint8_t flags,
                         uint16_t schema_id,
                         const void *payload,
                         size_t payload_size,
                         uint8_t *out_packet,
                         size_t out_capacity,
                         size_t *out_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_RUDP_WIRE_FRAME_H */
