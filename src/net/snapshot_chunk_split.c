/**
 * @file snapshot_chunk_split.c
 * @brief Snapshot chunk splitting.
 *
 * Non-static functions: 1 (split).
 */

#include "ferrum/net/snapshot_chunk.h"

int net_snapshot_chunk_split(const uint8_t *payload,
                             uint32_t payload_size,
                             uint32_t chunk_size,
                             net_chunk_header_t *headers_out,
                             uint32_t headers_cap,
                             uint32_t *chunk_count) {
    if (!payload || !headers_out || !chunk_count || chunk_size == 0) {
        return NET_CHUNK_ERR_INVALID;
    }

    /* Handle zero-length payload → 1 empty chunk. */
    uint32_t total = (payload_size + chunk_size - 1) / chunk_size;
    if (total == 0) { total = 1; }

    if (total > headers_cap) {
        return NET_CHUNK_ERR_CAPACITY;
    }

    *chunk_count = total;

    uint32_t offset = 0;
    for (uint32_t i = 0; i < total; i++) {
        uint32_t remaining = payload_size - offset;
        uint32_t len = remaining < chunk_size ? remaining : chunk_size;

        headers_out[i].chunk_index = (uint16_t)i;
        headers_out[i].chunk_total = (uint16_t)total;
        headers_out[i].offset = offset;
        headers_out[i].length = len;

        offset += len;
    }

    return NET_CHUNK_OK;
}
