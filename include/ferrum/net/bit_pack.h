#ifndef FERRUM_NET_BIT_PACK_H
#define FERRUM_NET_BIT_PACK_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Bit-packed packet header encode/decode helpers.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: operation succeeded. */
#define NET_BIT_PACK_OK 0
/** Status: invalid arguments. */
#define NET_BIT_PACK_ERR_INVALID -1
/** Status: buffer too small (header truncated). */
#define NET_BIT_PACK_ERR_SHORT -2
/** Status: packet length fields invalid (payload length exceeds buffer). */
#define NET_BIT_PACK_ERR_MALFORMED -3

/** Fixed size in bytes of the bit-pack header (schema_id + payload_size). */
#define NET_BIT_PACK_HEADER_SIZE 4u

/** Bit-pack header fields in host byte order. */
typedef struct net_bit_pack_header {
    uint16_t schema_id;
    uint16_t payload_size;
} net_bit_pack_header_t;

/**
 * @brief Encode a bit-pack header + payload into a byte buffer (network byte order).
 * @param header Header pointer (non-NULL).
 * @param payload Payload bytes pointer (may be NULL if payload_size==0).
 * @param payload_size Payload size in bytes.
 * @param out_bytes Destination buffer.
 * @param out_size Destination buffer size.
 * @param out_written Written size in bytes.
 * @return NET_BIT_PACK_OK on success or error code.
 */
int net_bit_pack_encode(const net_bit_pack_header_t *header,
                        const uint8_t *payload,
                        size_t payload_size,
                        uint8_t *out_bytes,
                        size_t out_size,
                        size_t *out_written);

/**
 * @brief Decode a bit-pack header and return a pointer to the payload bytes.
 * @param header Header pointer to receive decoded values.
 * @param bytes Input byte buffer.
 * @param size Input buffer size.
 * @param out_payload Pointer to payload bytes inside @p bytes.
 * @param out_payload_size Payload size from header.
 * @return NET_BIT_PACK_OK on success or error code.
 */
int net_bit_pack_decode(net_bit_pack_header_t *header,
                        const uint8_t *bytes,
                        size_t size,
                        const uint8_t **out_payload,
                        size_t *out_payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_BIT_PACK_H */
