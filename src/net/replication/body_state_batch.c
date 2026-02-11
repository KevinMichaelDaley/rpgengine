/**
 * @file body_state_batch.c
 * @brief Encode/decode for NET_REPL_SCHEMA_BODY_STATE_BATCH.
 */
#include <string.h>

#include "ferrum/net/replication/body_state_batch.h"

int net_repl_body_state_batch_encode(
    const uint8_t entries[][NET_REPL_BODY_STATE_PAYLOAD_SIZE],
    uint16_t count,
    uint8_t *out, size_t out_size, size_t *out_len) {
    if (!entries || count == 0u || count > NET_REPL_BODY_STATE_BATCH_MAX
        || !out || !out_len) {
        return NET_REPL_ERR_INVALID;
    }

    const size_t needed = 2u + (size_t)count * NET_REPL_BODY_STATE_PAYLOAD_SIZE;
    if (out_size < needed) {
        return NET_REPL_ERR_SHORT;
    }

    /* [count:u16 LE] */
    out[0] = (uint8_t)(count & 0xFFu);
    out[1] = (uint8_t)((count >> 8u) & 0xFFu);

    /* Concatenated 40-byte payloads. */
    memcpy(out + 2u, entries,
           (size_t)count * NET_REPL_BODY_STATE_PAYLOAD_SIZE);

    *out_len = needed;
    return NET_REPL_OK;
}

int net_repl_body_state_batch_decode(
    const uint8_t *payload, size_t payload_size,
    uint16_t *out_count, const uint8_t **out_entries) {
    if (!payload || !out_count || !out_entries) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < 2u) {
        return NET_REPL_ERR_SHORT;
    }

    uint16_t count = (uint16_t)payload[0]
                   | ((uint16_t)payload[1] << 8u);

    if (count == 0u || count > NET_REPL_BODY_STATE_BATCH_MAX) {
        return NET_REPL_ERR_INVALID;
    }

    const size_t needed = 2u + (size_t)count * NET_REPL_BODY_STATE_PAYLOAD_SIZE;
    if (payload_size < needed) {
        return NET_REPL_ERR_SHORT;
    }

    *out_count = count;
    *out_entries = payload + 2u;
    return NET_REPL_OK;
}
