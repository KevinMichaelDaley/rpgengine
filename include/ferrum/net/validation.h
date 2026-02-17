/**
 * @file validation.h
 * @brief Protocol validation and instrumentation counters.
 *
 * Provides a top-of-receive-path validator that checks:
 *   - Packet length (minimum header size)
 *   - Protocol ID match
 *   - Known schema ID
 *   - Payload size consistency
 *
 * All checks increment per-category counters in a stats struct
 * for monitoring and debugging.
 *
 * Ownership: schema whitelist is stored inline (max 64 entries).
 * No dynamic allocation.  NULL-safe.
 */

#ifndef FERRUM_NET_VALIDATION_H
#define FERRUM_NET_VALIDATION_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Return codes ──────────────────────────────────────────────── */

#define NET_VALIDATION_OK             0
#define NET_VALIDATION_ERR_INVALID   -1
#define NET_VALIDATION_ERR_TRUNCATED -2
#define NET_VALIDATION_ERR_PROTOCOL  -3
#define NET_VALIDATION_ERR_SCHEMA    -4
#define NET_VALIDATION_ERR_MALFORMED -5

/* ── Constants ─────────────────────────────────────────────────── */

/** Max registered schemas in the whitelist. */
#define NET_VALIDATION_MAX_SCHEMAS 64

/** Minimum packet size: 40-byte packet header + 8-byte wire frame header. */
#define NET_VALIDATION_MIN_PACKET  (NET_VALIDATION_PACKET_HEADER_SIZE + NET_VALIDATION_FRAME_HEADER_SIZE)

/** Packet header size: [protocol_id:4][sequence:2][ack:2][ack_bits:32] = 40. */
#define NET_VALIDATION_PACKET_HEADER_SIZE  40u

/** Wire frame header size: [flags:1][reserved:1][schema_id:2][payload_size:2][reserved:2] = 8. */
#define NET_VALIDATION_FRAME_HEADER_SIZE   8u

/* ── Types ─────────────────────────────────────────────────────── */

/**
 * @brief Instrumentation counters for packet validation.
 */
typedef struct net_validation_stats {
    uint64_t packets_total;      /**< Total packets checked. */
    uint64_t packets_valid;      /**< Packets that passed all checks. */
    uint64_t bytes_total;        /**< Total bytes across all packets. */
    uint64_t protocol_errors;    /**< Protocol ID mismatches. */
    uint64_t unknown_schemas;    /**< Unknown schema IDs. */
    uint64_t truncated_packets;  /**< Packets shorter than min size. */
    uint64_t malformed_packets;  /**< Payload size > remaining bytes. */
} net_validation_stats_t;

/**
 * @brief Validation context: protocol config + stats.
 */
typedef struct net_validation_ctx {
    uint32_t protocol_id;        /**< Expected protocol ID. */
    uint16_t schemas[NET_VALIDATION_MAX_SCHEMAS]; /**< Known schema IDs. */
    uint32_t schema_count;       /**< Number of registered schemas. */
    net_validation_stats_t stats; /**< Accumulated counters. */
} net_validation_ctx_t;

/* ── Public API ────────────────────────────────────────────────── */

/**
 * @brief Initialize a validation context.
 *
 * @param ctx          Context to initialize (non-NULL).
 * @param protocol_id  Expected protocol ID for incoming packets.
 * @param schemas      Array of known schema IDs (may be NULL if count is 0).
 * @param schema_count Number of schemas (clamped to MAX_SCHEMAS).
 */
void net_validation_init(net_validation_ctx_t *ctx,
                         uint32_t protocol_id,
                         const uint16_t *schemas,
                         uint32_t schema_count);

/**
 * @brief Validate a raw incoming packet.
 *
 * Checks (in order):
 *   1. Packet length ≥ 48 bytes (40-byte header + 8-byte frame header)
 *   2. Protocol ID matches
 *   3. Schema ID is in the whitelist
 *   4. Payload size ≤ remaining bytes
 *
 * Increments the appropriate counter for each failure.
 *
 * @param ctx     Validation context (non-NULL).
 * @param packet  Raw packet bytes (non-NULL).
 * @param size    Packet size in bytes.
 * @return NET_VALIDATION_OK if valid, or an error code.
 */
int net_validation_check(net_validation_ctx_t *ctx,
                         const uint8_t *packet,
                         size_t size);

/**
 * @brief Reset all instrumentation counters to zero.
 *
 * @param ctx  Validation context (NULL-safe, no-op if NULL).
 */
void net_validation_reset_stats(net_validation_ctx_t *ctx);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_VALIDATION_H */
