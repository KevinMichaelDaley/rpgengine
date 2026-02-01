#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/join.h"

static void write_u32_be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static uint32_t read_u32_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

int net_repl_join_encode(const net_repl_join_t *msg, uint8_t *out_payload, size_t out_size) {
    if (!msg || !out_payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (out_size < (size_t)NET_REPL_JOIN_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    write_u32_be(out_payload, msg->client_nonce);
    return NET_REPL_OK;
}

int net_repl_join_decode(net_repl_join_t *msg, const uint8_t *payload, size_t payload_size) {
    if (!msg || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < (size_t)NET_REPL_JOIN_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    msg->client_nonce = read_u32_be(payload);
    return NET_REPL_OK;
}
