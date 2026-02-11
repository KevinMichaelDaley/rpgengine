#include "ferrum/net/packet_header.h"

static void write_u64_be(uint8_t *out, uint64_t value) {
    out[0] = (uint8_t)((value >> 56) & 0xFFu);
    out[1] = (uint8_t)((value >> 48) & 0xFFu);
    out[2] = (uint8_t)((value >> 40) & 0xFFu);
    out[3] = (uint8_t)((value >> 32) & 0xFFu);
    out[4] = (uint8_t)((value >> 24) & 0xFFu);
    out[5] = (uint8_t)((value >> 16) & 0xFFu);
    out[6] = (uint8_t)((value >> 8) & 0xFFu);
    out[7] = (uint8_t)(value & 0xFFu);
}

static void write_u32_be(uint8_t *out, uint32_t value) {
    out[0] = (uint8_t)((value >> 24) & 0xFFu);
    out[1] = (uint8_t)((value >> 16) & 0xFFu);
    out[2] = (uint8_t)((value >> 8) & 0xFFu);
    out[3] = (uint8_t)(value & 0xFFu);
}

static void write_u16_be(uint8_t *out, uint16_t value) {
    out[0] = (uint8_t)((value >> 8) & 0xFFu);
    out[1] = (uint8_t)(value & 0xFFu);
}

static uint64_t read_u64_be(const uint8_t *bytes) {
    return ((uint64_t)bytes[0] << 56) |
           ((uint64_t)bytes[1] << 48) |
           ((uint64_t)bytes[2] << 40) |
           ((uint64_t)bytes[3] << 32) |
           ((uint64_t)bytes[4] << 24) |
           ((uint64_t)bytes[5] << 16) |
           ((uint64_t)bytes[6] << 8) |
           (uint64_t)bytes[7];
}

static uint32_t read_u32_be(const uint8_t *bytes) {
    return ((uint32_t)bytes[0] << 24) |
           ((uint32_t)bytes[1] << 16) |
           ((uint32_t)bytes[2] << 8) |
           (uint32_t)bytes[3];
}

static uint16_t read_u16_be(const uint8_t *bytes) {
    return (uint16_t)(((uint16_t)bytes[0] << 8) | (uint16_t)bytes[1]);
}

int net_packet_header_encode(const net_packet_header_t *header, uint8_t *out_bytes, size_t out_size) {
    if (!header || !out_bytes) {
        return NET_PACKET_HEADER_ERR_INVALID;
    }
    if (out_size < NET_PACKET_HEADER_SIZE) {
        return NET_PACKET_HEADER_ERR_SHORT;
    }

    write_u32_be(out_bytes, header->protocol_id);
    write_u16_be(out_bytes + 4, header->sequence);
    write_u16_be(out_bytes + 6, header->ack);
    write_u64_be(out_bytes + 8,  header->ack_bits[0]);
    write_u64_be(out_bytes + 16, header->ack_bits[1]);
    write_u64_be(out_bytes + 24, header->ack_bits[2]);
    write_u64_be(out_bytes + 32, header->ack_bits[3]);
    return NET_PACKET_HEADER_OK;
}

int net_packet_header_decode(net_packet_header_t *header, const uint8_t *bytes, size_t size) {
    if (!header || !bytes) {
        return NET_PACKET_HEADER_ERR_INVALID;
    }
    if (size < NET_PACKET_HEADER_SIZE) {
        return NET_PACKET_HEADER_ERR_SHORT;
    }

    header->protocol_id = read_u32_be(bytes);
    header->sequence = read_u16_be(bytes + 4);
    header->ack = read_u16_be(bytes + 6);
    header->ack_bits[0] = read_u64_be(bytes + 8);
    header->ack_bits[1] = read_u64_be(bytes + 16);
    header->ack_bits[2] = read_u64_be(bytes + 24);
    header->ack_bits[3] = read_u64_be(bytes + 32);
    return NET_PACKET_HEADER_OK;
}
