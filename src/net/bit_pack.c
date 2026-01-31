#include "ferrum/net/bit_pack.h"

#include <string.h>

static void write_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint16_t read_u16_be(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

int net_bit_pack_encode(const net_bit_pack_header_t *header,
                        const uint8_t *payload,
                        size_t payload_size,
                        uint8_t *out_bytes,
                        size_t out_size,
                        size_t *out_written) {
    if (!header || !out_bytes || !out_written) {
        return NET_BIT_PACK_ERR_INVALID;
    }
    if (payload_size != (size_t)header->payload_size) {
        return NET_BIT_PACK_ERR_INVALID;
    }
    if (payload_size > 0u && !payload) {
        return NET_BIT_PACK_ERR_INVALID;
    }

    size_t required = (size_t)NET_BIT_PACK_HEADER_SIZE + payload_size;
    if (out_size < required) {
        return NET_BIT_PACK_ERR_SHORT;
    }

    write_u16_be(out_bytes, header->schema_id);
    write_u16_be(out_bytes + 2u, header->payload_size);
    if (payload_size > 0u) {
        memcpy(out_bytes + NET_BIT_PACK_HEADER_SIZE, payload, payload_size);
    }
    *out_written = required;
    return NET_BIT_PACK_OK;
}

int net_bit_pack_decode(net_bit_pack_header_t *header,
                        const uint8_t *bytes,
                        size_t size,
                        const uint8_t **out_payload,
                        size_t *out_payload_size) {
    if (!header || !bytes || !out_payload || !out_payload_size) {
        return NET_BIT_PACK_ERR_INVALID;
    }
    if (size < NET_BIT_PACK_HEADER_SIZE) {
        return NET_BIT_PACK_ERR_SHORT;
    }

    header->schema_id = read_u16_be(bytes);
    header->payload_size = read_u16_be(bytes + 2u);

    size_t payload_size = (size_t)header->payload_size;
    if (payload_size > size - NET_BIT_PACK_HEADER_SIZE) {
        return NET_BIT_PACK_ERR_MALFORMED;
    }

    *out_payload = bytes + NET_BIT_PACK_HEADER_SIZE;
    *out_payload_size = payload_size;
    return NET_BIT_PACK_OK;
}
