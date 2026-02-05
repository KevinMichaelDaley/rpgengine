#include <string.h>

#include "ferrum/server/net/inbound_message.h"

static void write_u16_le_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)(v & 0xFFu);
    out[1] = (uint8_t)((v >> 8u) & 0xFFu);
}

static uint16_t read_u16_le_(const uint8_t *in) {
    return (uint16_t)in[0] | ((uint16_t)in[1] << 8u);
}

bool fr_server_net_inbound_message_encode(uint16_t client_id,
                                         bool reliable,
                                         uint16_t schema_id,
                                         const void *payload,
                                         size_t payload_size,
                                         uint8_t *out,
                                         size_t out_cap,
                                         size_t *out_size) {
    if (!out || !out_size) {
        return false;
    }
    if (payload_size > 0u && !payload) {
        return false;
    }

    const size_t header_size = 6u;
    if (out_cap < header_size + payload_size) {
        return false;
    }

    write_u16_le_(out + 0u, client_id);
    write_u16_le_(out + 2u, schema_id);
    out[4] = (uint8_t)((reliable ? 1u : 0u) & 0x1u);
    out[5] = 0u;
    if (payload_size > 0u) {
        memcpy(out + header_size, payload, payload_size);
    }
    *out_size = header_size + payload_size;
    return true;
}

bool fr_server_net_inbound_message_decode(fr_server_net_inbound_message_view_t *out,
                                         const uint8_t *msg,
                                         size_t msg_size) {
    if (!out || !msg) {
        return false;
    }

    const size_t header_size = 6u;
    if (msg_size < header_size) {
        return false;
    }

    out->client_id = read_u16_le_(msg + 0u);
    out->schema_id = read_u16_le_(msg + 2u);
    out->reliable = (msg[4] & 0x1u) ? true : false;
    out->payload = msg + header_size;
    out->payload_size = msg_size - header_size;
    return true;
}
