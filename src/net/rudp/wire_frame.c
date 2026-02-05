#include <string.h>

#include "ferrum/net/rudp/wire_frame.h"

static uint16_t read_u16_be_(const uint8_t *in) {
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

static void write_u16_be_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xFFu);
    out[1] = (uint8_t)(v & 0xFFu);
}

int net_rudp_wire_decode(net_packet_header_t *out_header,
                         net_rudp_wire_frame_view_t *out_frame,
                         const uint8_t *packet,
                         size_t packet_size) {
    if (!out_header || !out_frame || !packet) {
        return NET_RUDP_WIRE_ERR_INVALID;
    }
    if (packet_size < NET_PACKET_HEADER_SIZE + NET_RUDP_WIRE_FRAME_HEADER_SIZE) {
        return NET_RUDP_WIRE_ERR_SHORT;
    }

    int rc = net_packet_header_decode(out_header, packet, packet_size);
    if (rc != NET_PACKET_HEADER_OK) {
        return NET_RUDP_WIRE_ERR_PROTOCOL;
    }

    const uint8_t *frame = packet + NET_PACKET_HEADER_SIZE;
    const uint8_t flags = frame[0];
    const uint16_t schema_id = read_u16_be_(frame + 2);
    const uint16_t payload_size_u16 = read_u16_be_(frame + 4);

    const size_t total_needed = NET_PACKET_HEADER_SIZE + NET_RUDP_WIRE_FRAME_HEADER_SIZE + (size_t)payload_size_u16;
    if (packet_size < total_needed) {
        return NET_RUDP_WIRE_ERR_SHORT;
    }

    out_frame->flags = flags;
    out_frame->schema_id = schema_id;
    out_frame->payload = frame + NET_RUDP_WIRE_FRAME_HEADER_SIZE;
    out_frame->payload_size = (size_t)payload_size_u16;
    return NET_RUDP_WIRE_OK;
}

int net_rudp_wire_encode(const net_packet_header_t *header,
                         uint8_t flags,
                         uint16_t schema_id,
                         const void *payload,
                         size_t payload_size,
                         uint8_t *out_packet,
                         size_t out_capacity,
                         size_t *out_size) {
    if (!header || !out_packet || !out_size) {
        return NET_RUDP_WIRE_ERR_INVALID;
    }
    if (payload_size > 0u && !payload) {
        return NET_RUDP_WIRE_ERR_INVALID;
    }
    if (payload_size > 0xFFFFu) {
        return NET_RUDP_WIRE_ERR_INVALID;
    }
    if (out_capacity < NET_PACKET_HEADER_SIZE + NET_RUDP_WIRE_FRAME_HEADER_SIZE + payload_size) {
        return NET_RUDP_WIRE_ERR_SHORT;
    }

    int rc = net_packet_header_encode(header, out_packet, out_capacity);
    if (rc != NET_PACKET_HEADER_OK) {
        return NET_RUDP_WIRE_ERR_PROTOCOL;
    }

    uint8_t *frame = out_packet + NET_PACKET_HEADER_SIZE;
    frame[0] = flags;
    frame[1] = 0u;
    write_u16_be_(frame + 2, schema_id);
    write_u16_be_(frame + 4, (uint16_t)payload_size);
    write_u16_be_(frame + 6, 0u);
    if (payload_size > 0u) {
        memcpy(frame + NET_RUDP_WIRE_FRAME_HEADER_SIZE, payload, payload_size);
    }

    *out_size = NET_PACKET_HEADER_SIZE + NET_RUDP_WIRE_FRAME_HEADER_SIZE + payload_size;
    return NET_RUDP_WIRE_OK;
}
