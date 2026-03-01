/**
 * @file mesh_data_send.c
 * @brief MESH_DATA send helper — splits FVMA blob into chunks.
 */
#include "ferrum/net/replication/mesh_data.h"

uint32_t net_repl_mesh_data_send(uint16_t body_id,
                                 const uint8_t *fvma_data,
                                 uint32_t fvma_size,
                                 bool (*send_fn)(const uint8_t *wire,
                                                 size_t wire_len,
                                                 void *user_data),
                                 void *user_data) {
    if (!fvma_data || !send_fn || fvma_size == 0) return 0;
    if (fvma_size > NET_REPL_MESH_MAX_TOTAL) return 0;

    /* Compute chunk count. */
    uint32_t total_chunks = (fvma_size + NET_REPL_MESH_CHUNK_MAX - 1)
                            / NET_REPL_MESH_CHUNK_MAX;
    if (total_chunks > UINT16_MAX) return 0;

    /* Encode and send each chunk via the schema-prefixed wire format. */
    uint8_t wire[2u + NET_REPL_MESH_CHUNK_HEADER_SIZE + NET_REPL_MESH_CHUNK_MAX];
    uint32_t offset = 0;

    for (uint32_t i = 0; i < total_chunks; i++) {
        uint32_t remaining = fvma_size - offset;
        uint16_t chunk_payload = (remaining > NET_REPL_MESH_CHUNK_MAX)
                                 ? (uint16_t)NET_REPL_MESH_CHUNK_MAX
                                 : (uint16_t)remaining;

        net_repl_mesh_chunk_t chunk = {
            .body_id       = body_id,
            .chunk_index   = (uint16_t)i,
            .total_chunks  = (uint16_t)total_chunks,
            .total_size    = fvma_size,
            .payload       = fvma_data + offset,
            .payload_size  = chunk_payload,
        };

        /* Schema prefix (little-endian, matches existing convention). */
        wire[0] = (uint8_t)(NET_REPL_SCHEMA_MESH_DATA & 0xFFu);
        wire[1] = (uint8_t)((NET_REPL_SCHEMA_MESH_DATA >> 8u) & 0xFFu);

        uint32_t n = net_repl_mesh_chunk_encode(&chunk, wire + 2,
                                                sizeof(wire) - 2);
        if (n == 0) return i;

        if (!send_fn(wire, 2u + n, user_data)) return i;

        offset += chunk_payload;
    }

    return total_chunks;
}
