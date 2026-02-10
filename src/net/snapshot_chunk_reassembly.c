/**
 * @file snapshot_chunk_reassembly.c
 * @brief Snapshot chunk reassembly.
 *
 * Non-static functions: 3 (init, push, reset).
 */

#include "ferrum/net/snapshot_chunk.h"
#include <string.h>

void net_chunk_reassembly_init(net_chunk_reassembly_t *reasm,
                               uint8_t *buffer,
                               uint32_t capacity) {
    if (!reasm) { return; }
    reasm->buffer = buffer;
    reasm->buffer_capacity = capacity;
    reasm->total_size = 0;
    reasm->chunks_expected = 0;
    reasm->chunks_received = 0;
    reasm->received_mask = 0;
}

int net_chunk_reassembly_push(net_chunk_reassembly_t *reasm,
                              const net_chunk_header_t *header,
                              const uint8_t *data,
                              uint32_t length) {
    if (!reasm || !header || !data) { return NET_CHUNK_ERR_INVALID; }

    /* On first chunk, record expected count and compute total size. */
    if (reasm->chunks_expected == 0) {
        reasm->chunks_expected = header->chunk_total;
    }

    uint16_t idx = header->chunk_index;

    /* Ignore duplicate. */
    if (reasm->received_mask & ((uint64_t)1 << idx)) {
        /* Already received — check if now complete. */
        if (reasm->chunks_received >= reasm->chunks_expected) {
            return NET_CHUNK_READY;
        }
        return NET_CHUNK_NOT_READY;
    }

    /* Copy data into buffer at the correct offset. */
    if (header->offset + length <= reasm->buffer_capacity) {
        memcpy(reasm->buffer + header->offset, data, length);
    }

    reasm->received_mask |= ((uint64_t)1 << idx);
    reasm->chunks_received++;

    /* Track total size as max(offset + length). */
    uint32_t end = header->offset + length;
    if (end > reasm->total_size) {
        reasm->total_size = end;
    }

    if (reasm->chunks_received >= reasm->chunks_expected) {
        return NET_CHUNK_READY;
    }
    return NET_CHUNK_NOT_READY;
}

void net_chunk_reassembly_reset(net_chunk_reassembly_t *reasm) {
    if (!reasm) { return; }
    reasm->total_size = 0;
    reasm->chunks_expected = 0;
    reasm->chunks_received = 0;
    reasm->received_mask = 0;
}
