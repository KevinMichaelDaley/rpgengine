/**
 * @file mesh_data.h
 * @brief MESH_DATA message: server -> client (reliable, chunked).
 *
 * Transfers serialized mesh geometry (FVMA format) from server to client.
 * Large meshes are split into chunks that fit within the reliable channel's
 * message size limit.  The client reassembles chunks by body_id before
 * building a renderable mesh.
 *
 * Wire layout per chunk (big-endian):
 *   [2] body_id         — which body this mesh belongs to
 *   [2] chunk_index     — 0-based chunk number
 *   [2] total_chunks    — total number of chunks for this mesh
 *   [4] total_size      — total FVMA byte count (same in every chunk)
 *   [N] payload         — chunk payload (N = chunk_size, last may be smaller)
 *
 * Header size: 10 bytes.  Max payload per chunk: NET_REPL_MESH_CHUNK_MAX.
 */
#ifndef FERRUM_NET_REPLICATION_MESH_DATA_H
#define FERRUM_NET_REPLICATION_MESH_DATA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/common.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ */
/* Constants                                                           */
/* ------------------------------------------------------------------ */

/** Per-chunk header size (body_id + chunk_idx + total_chunks + total_size). */
#define NET_REPL_MESH_CHUNK_HEADER_SIZE 10u

/** Max payload bytes per chunk.  Must fit inside a single RUDP reliable
 *  stream frame: 464 (RUDP payload) - 4 (stream hdr) - 2 (schema) - 10
 *  (chunk hdr) = 448.  Round down to 440 for alignment headroom. */
#define NET_REPL_MESH_CHUNK_MAX 440u

/** Max supported total mesh size (256 KiB). */
#define NET_REPL_MESH_MAX_TOTAL (256u * 1024u)

/* ------------------------------------------------------------------ */
/* Chunk encode / decode                                               */
/* ------------------------------------------------------------------ */

/** @brief A single mesh data chunk (one fragment of an FVMA blob). */
typedef struct net_repl_mesh_chunk {
    uint16_t body_id;       /**< Body this mesh belongs to. */
    uint16_t chunk_index;   /**< 0-based chunk number. */
    uint16_t total_chunks;  /**< Total chunks for this mesh. */
    uint32_t total_size;    /**< Total FVMA byte count. */
    const uint8_t *payload; /**< Pointer to chunk payload (not owned). */
    uint16_t payload_size;  /**< Size of this chunk's payload. */
} net_repl_mesh_chunk_t;

/**
 * @brief Encode a mesh chunk to wire format.
 * @param chunk   Source chunk. payload and payload_size must be valid.
 * @param out     Output buffer.
 * @param out_cap Output buffer capacity.
 * @return Bytes written, or 0 on error.
 */
uint32_t net_repl_mesh_chunk_encode(const net_repl_mesh_chunk_t *chunk,
                                    uint8_t *out, size_t out_cap);

/**
 * @brief Decode a mesh chunk from wire format.
 * @param chunk    Destination. payload will point into @p data.
 * @param data     Wire buffer.
 * @param data_len Wire buffer size.
 * @return NET_REPL_OK on success.
 */
int net_repl_mesh_chunk_decode(net_repl_mesh_chunk_t *chunk,
                               const uint8_t *data, size_t data_len);

/* ------------------------------------------------------------------ */
/* Chunk sender (server-side helper)                                   */
/* ------------------------------------------------------------------ */

/**
 * @brief Split an FVMA blob into chunks and call a send callback for each.
 *
 * @param body_id    Body that owns this mesh.
 * @param fvma_data  Serialized FVMA data.
 * @param fvma_size  Size in bytes.
 * @param send_fn    Callback invoked for each encoded chunk.
 *                   Receives (wire_buf, wire_len, user_data).
 *                   Return true to continue, false to abort.
 * @param user_data  Passed through to send_fn.
 * @return Number of chunks sent, or 0 on error.
 *
 * Ownership: fvma_data is not modified and not freed by this function.
 */
uint32_t net_repl_mesh_data_send(uint16_t body_id,
                                 const uint8_t *fvma_data,
                                 uint32_t fvma_size,
                                 bool (*send_fn)(const uint8_t *wire,
                                                 size_t wire_len,
                                                 void *user_data),
                                 void *user_data);

/* ------------------------------------------------------------------ */
/* Chunk reassembly (client-side helper)                               */
/* ------------------------------------------------------------------ */

/** @brief Per-body reassembly slot. */
typedef struct net_repl_mesh_reassembly {
    uint16_t body_id;        /**< Body being reassembled. */
    uint16_t total_chunks;   /**< Expected chunk count. */
    uint32_t total_size;     /**< Expected total FVMA size. */
    uint32_t received_mask;  /**< Bitmask of received chunks (max 32). */
    uint8_t *buffer;         /**< Heap-allocated reassembly buffer (owned). */
} net_repl_mesh_reassembly_t;

/** @brief Fixed-capacity reassembly table. */
typedef struct net_repl_mesh_reassembly_table {
    net_repl_mesh_reassembly_t *slots;   /**< Array of slots (owned). */
    uint32_t capacity;                    /**< Number of slots. */
} net_repl_mesh_reassembly_table_t;

/**
 * @brief Initialize reassembly table.
 * @param table    Table to initialize.
 * @param capacity Max simultaneous meshes being reassembled.
 * @return true on success.
 */
bool net_repl_mesh_reassembly_init(net_repl_mesh_reassembly_table_t *table,
                                   uint32_t capacity);

/**
 * @brief Destroy reassembly table and free all buffers.
 */
void net_repl_mesh_reassembly_destroy(net_repl_mesh_reassembly_table_t *table);

/**
 * @brief Push a received chunk into the reassembly table.
 *
 * @param table    Reassembly table.
 * @param chunk    Decoded chunk (payload must be valid during this call).
 * @param out_data If reassembly completes, set to the reassembled buffer.
 *                 Caller takes ownership and must free() it.
 * @param out_size Set to total FVMA size on completion.
 * @return true if this chunk completed the mesh (out_data/out_size valid).
 *         false if more chunks are needed or on error.
 */
bool net_repl_mesh_reassembly_push(net_repl_mesh_reassembly_table_t *table,
                                   const net_repl_mesh_chunk_t *chunk,
                                   uint8_t **out_data,
                                   uint32_t *out_size);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_REPLICATION_MESH_DATA_H */
