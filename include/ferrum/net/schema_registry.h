#ifndef FERRUM_NET_SCHEMA_REGISTRY_H
#define FERRUM_NET_SCHEMA_REGISTRY_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Schema ID registry for replication payloads.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status: operation succeeded. */
#define NET_SCHEMA_REGISTRY_OK 0
/** Status: invalid arguments. */
#define NET_SCHEMA_REGISTRY_ERR_INVALID -1
/** Status: schema not registered. */
#define NET_SCHEMA_REGISTRY_ERR_UNKNOWN_SCHEMA -2
/** Status: payload length does not match schema. */
#define NET_SCHEMA_REGISTRY_ERR_PAYLOAD_LENGTH -3
/** Status: registry full. */
#define NET_SCHEMA_REGISTRY_ERR_FULL -4
/** Status: packet decode failed (truncated/malformed). */
#define NET_SCHEMA_REGISTRY_ERR_PACKET -5

/** Max registered schemas per registry. */
#define NET_SCHEMA_REGISTRY_MAX_SCHEMAS 256u

/** Schema registry (fixed-size, no dynamic allocation). */
typedef struct net_schema_registry {
    uint16_t schema_ids[NET_SCHEMA_REGISTRY_MAX_SCHEMAS];
    uint16_t payload_sizes[NET_SCHEMA_REGISTRY_MAX_SCHEMAS];
    size_t count;
} net_schema_registry_t;

/**
 * @brief Initialize the registry.
 * @param registry Registry pointer (non-NULL).
 */
void net_schema_registry_init(net_schema_registry_t *registry);

/**
 * @brief Register a schema ID and fixed payload size.
 * @param registry Registry pointer.
 * @param schema_id Schema ID.
 * @param payload_size_bytes Expected payload size in bytes.
 * @return NET_SCHEMA_REGISTRY_OK on success or error code.
 */
int net_schema_registry_register(net_schema_registry_t *registry, uint16_t schema_id, size_t payload_size_bytes);

/**
 * @brief Decode a schema packet and validate schema ID and payload size.
 *
 * Packet format:
 *  - schema_id (u16, big-endian)
 *  - payload_size (u16, big-endian)
 *  - payload bytes
 *
 * @param registry Registry pointer.
 * @param bytes Packet bytes.
 * @param size Packet size in bytes.
 * @param out_schema_id Decoded schema ID.
 * @param out_payload Pointer into @p bytes for payload bytes.
 * @param out_payload_size Payload size in bytes.
 * @return NET_SCHEMA_REGISTRY_OK on success or error code.
 */
int net_schema_registry_decode_packet(const net_schema_registry_t *registry,
                                     const uint8_t *bytes,
                                     size_t size,
                                     uint16_t *out_schema_id,
                                     const uint8_t **out_payload,
                                     size_t *out_payload_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_SCHEMA_REGISTRY_H */
