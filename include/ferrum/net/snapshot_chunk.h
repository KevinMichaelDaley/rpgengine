/**
 * @file snapshot_chunk.h
 * @brief Snapshot chunking and reassembly for baseline recovery.
 *
 * Splits large snapshot payloads into fixed-size chunks with headers
 * and reassembles them on the receiver.  Designed to sit above the
 * RUDP reliable-ordered channel — each chunk is sent as a separate
 * reliable message.
 *
 * Ownership: all buffers are caller-provided.  No dynamic allocation.
 * NULL-safe: all public functions check for NULL inputs.
 *
 * Reassembly uses a 64-bit bitmask, supporting up to 64 chunks
 * per snapshot (matching RUDP fragment limit).
 */

#ifndef FERRUM_NET_SNAPSHOT_CHUNK_H
#define FERRUM_NET_SNAPSHOT_CHUNK_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Status / return codes ─────────────────────────────────────── */

#define NET_CHUNK_OK            0
#define NET_CHUNK_READY         1   /**< All chunks received. */
#define NET_CHUNK_NOT_READY     2   /**< Still waiting for chunks. */
#define NET_CHUNK_ERR_INVALID  -1
#define NET_CHUNK_ERR_CAPACITY -2

/* ── Types ─────────────────────────────────────────────────────── */

/**
 * @brief Header describing one chunk of a split payload.
 *
 * Produced by net_snapshot_chunk_split() and consumed by
 * net_chunk_reassembly_push().
 */
typedef struct net_chunk_header {
    uint16_t chunk_index;   /**< 0-based index of this chunk. */
    uint16_t chunk_total;   /**< Total number of chunks in the message. */
    uint32_t offset;        /**< Byte offset into the original payload. */
    uint32_t length;        /**< Length of this chunk's data in bytes. */
} net_chunk_header_t;

/**
 * @brief Reassembly state for receiving chunks.
 *
 * Caller provides the reassembly buffer.  Supports up to 64 chunks
 * via a 64-bit received_mask.
 */
typedef struct net_chunk_reassembly {
    uint8_t *buffer;            /**< Caller-owned reassembly buffer. */
    uint32_t buffer_capacity;   /**< Size of the reassembly buffer. */
    uint32_t total_size;        /**< Total payload size (from chunks). */
    uint16_t chunks_expected;   /**< Total chunk count (from header). */
    uint16_t chunks_received;   /**< Number of unique chunks received. */
    uint64_t received_mask;     /**< Bitmask of received chunk indices. */
} net_chunk_reassembly_t;

/* ── Public API: splitting ─────────────────────────────────────── */

/**
 * @brief Split a payload into chunks and populate header array.
 *
 * Does NOT copy payload data — headers describe offsets into the
 * original buffer.  The caller sends each chunk's data slice
 * (payload + header[i].offset, header[i].length) separately.
 *
 * @param payload       Source payload buffer (non-NULL).
 * @param payload_size  Size of the payload in bytes.
 * @param chunk_size    Maximum bytes per chunk (must be > 0).
 * @param headers_out   Output header array (non-NULL).
 * @param headers_cap   Capacity of headers_out.
 * @param chunk_count   Output: number of chunks produced (non-NULL).
 * @return NET_CHUNK_OK on success,
 *         NET_CHUNK_ERR_CAPACITY if headers_cap too small,
 *         NET_CHUNK_ERR_INVALID if any argument is NULL or chunk_size is 0.
 */
int net_snapshot_chunk_split(const uint8_t *payload,
                             uint32_t payload_size,
                             uint32_t chunk_size,
                             net_chunk_header_t *headers_out,
                             uint32_t headers_cap,
                             uint32_t *chunk_count);

/* ── Public API: reassembly ────────────────────────────────────── */

/**
 * @brief Initialize a chunk reassembly context.
 *
 * @param reasm    Reassembly state to initialize (non-NULL).
 * @param buffer   Caller-owned buffer for reassembled payload.
 * @param capacity Size of the buffer.
 */
void net_chunk_reassembly_init(net_chunk_reassembly_t *reasm,
                               uint8_t *buffer,
                               uint32_t capacity);

/**
 * @brief Push a received chunk into the reassembly context.
 *
 * Copies chunk data into the reassembly buffer at the correct offset.
 * Duplicate chunks (same index) are silently ignored.
 *
 * @param reasm   Reassembly state (non-NULL).
 * @param header  Chunk header (non-NULL).
 * @param data    Chunk payload data (non-NULL).
 * @param length  Length of chunk data (should match header->length).
 * @return NET_CHUNK_READY if all chunks are now received,
 *         NET_CHUNK_NOT_READY if still waiting,
 *         NET_CHUNK_ERR_INVALID if any argument is NULL.
 */
int net_chunk_reassembly_push(net_chunk_reassembly_t *reasm,
                              const net_chunk_header_t *header,
                              const uint8_t *data,
                              uint32_t length);

/**
 * @brief Reset reassembly state for a new message.
 *
 * @param reasm  Reassembly state (NULL-safe, no-op if NULL).
 */
void net_chunk_reassembly_reset(net_chunk_reassembly_t *reasm);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_SNAPSHOT_CHUNK_H */
