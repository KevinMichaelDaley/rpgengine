#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/replication/welcome.h"

static void write_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t read_u16_be(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

int net_repl_welcome_encode(const net_repl_welcome_t *msg, uint8_t *out_payload, size_t out_size) {
    if (!msg || !out_payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (out_size < (size_t)NET_REPL_WELCOME_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    write_u16_be(out_payload + 0, msg->expected_entities);
    write_u16_be(out_payload + 2, msg->tick_hz);
    return NET_REPL_OK;
}

int net_repl_welcome_decode(net_repl_welcome_t *msg, const uint8_t *payload, size_t payload_size) {
    if (!msg || !payload) {
        return NET_REPL_ERR_INVALID;
    }
    if (payload_size < (size_t)NET_REPL_WELCOME_PAYLOAD_SIZE) {
        return NET_REPL_ERR_SHORT;
    }

    msg->expected_entities = read_u16_be(payload + 0);
    msg->tick_hz = read_u16_be(payload + 2);
    return NET_REPL_OK;
}
