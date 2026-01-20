#ifndef FERRUM_NET_PACKET_HEADER_H
#define FERRUM_NET_PACKET_HEADER_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Packet header encode/decode helpers for the network protocol.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: encode/decode succeeded. */
#define NET_PACKET_HEADER_OK 0
/** Status: invalid arguments. */
#define NET_PACKET_HEADER_ERR_INVALID -1
/** Status: buffer too small. */
#define NET_PACKET_HEADER_ERR_SHORT -2

/** Fixed size in bytes of the packet header. */
#define NET_PACKET_HEADER_SIZE 12u

/** Packet header fields in host byte order. */
typedef struct net_packet_header {
    uint32_t protocol_id;
    uint16_t sequence;
    uint16_t ack;
    uint32_t ack_bits;
} net_packet_header_t;

/**
 * @brief Encode a packet header into a byte buffer (network byte order).
 * @param header Header pointer (non-NULL).
 * @param out_bytes Destination buffer.
 * @param out_size Destination buffer size in bytes.
 * @return NET_PACKET_HEADER_OK on success or error code.
 */
int net_packet_header_encode(const net_packet_header_t *header, uint8_t *out_bytes, size_t out_size);

/**
 * @brief Decode a packet header from a byte buffer (network byte order).
 * @param header Header pointer to receive decoded values.
 * @param bytes Input byte buffer.
 * @param size Input buffer size in bytes.
 * @return NET_PACKET_HEADER_OK on success or error code.
 */
int net_packet_header_decode(net_packet_header_t *header, const uint8_t *bytes, size_t size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_PACKET_HEADER_H */
