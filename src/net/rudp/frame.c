#include <string.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/wire_frame.h"

#define NET_RUDP_FRAME_SIZE NET_RUDP_WIRE_FRAME_HEADER_SIZE

#define NET_RUDP_FLAG_RELIABLE NET_RUDP_WIRE_FLAG_RELIABLE
#define NET_RUDP_FLAG_FRAGMENT NET_RUDP_WIRE_FLAG_FRAGMENT

#define NET_RUDP_FRAG_HDR_SIZE 8u
#define NET_RUDP_FRAG_MAX 64u

static uint16_t read_u16_be(const uint8_t *in) {
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
}

static uint64_t mask_for_count_(uint16_t count) {
    if (count == 0u) {
        return 0u;
    }
    if (count >= 64u) {
        return ~(uint64_t)0u;
    }
    return ((uint64_t)1u << (uint64_t)count) - 1u;
}

int net_rudp_peer_receive(net_rudp_peer_t *peer,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint8_t *out_reliable,
                          uint16_t *out_schema_id,
                          uint8_t *out_payload,
                          size_t out_payload_capacity,
                          size_t *out_payload_size) {
    if (!peer || !packet || !out_reliable || !out_schema_id || !out_payload || !out_payload_size) {
        return NET_RUDP_ERR_INVALID;
    }

    net_packet_header_t header;
    net_rudp_wire_frame_view_t frame_view;
    int rc = net_rudp_wire_decode(&header, &frame_view, packet, packet_size);
    if (rc != NET_RUDP_WIRE_OK) {
        return (rc == NET_RUDP_WIRE_ERR_SHORT) ? NET_RUDP_ERR_SHORT : NET_RUDP_ERR_PROTOCOL;
    }
    if (header.protocol_id != peer->protocol_id) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    /* Retire ACKed reliable sends. */
    uint16_t ack = header.ack;
    uint32_t ack_bits = header.ack_bits;
    for (size_t i = 0u; i < peer->send_slot_count; ++i) {
        if (!peer->send_slots || !peer->send_slots[i].used) {
            continue;
        }
        uint16_t seq = peer->send_slots[i].sequence;
        if (seq == ack) {
            peer->send_slots[i].used = 0u;
            continue;
        }
        uint16_t delta = (uint16_t)(ack - seq);
        if (delta >= 1u && delta <= 32u) {
            uint32_t bit = 1u << (uint32_t)(delta - 1u);
            if (ack_bits & bit) {
                peer->send_slots[i].used = 0u;
            }
        }
    }

    const uint8_t flags = frame_view.flags;
    const uint16_t schema_id = frame_view.schema_id;
    const size_t payload_size = frame_view.payload_size;

    /* Only reliable packets participate in the ACK/duplicate window.
       This prevents high-rate unreliable traffic from evicting older
       reliable packets (WELCOME/SPAWN) and making retransmits permanently
       out-of-window.
     */
    if (flags & NET_RUDP_FLAG_RELIABLE) {
        int wrc = net_ack_window_receive(&peer->recv_window, header.sequence);
        if (wrc == NET_ACK_WINDOW_DUPLICATE || wrc == NET_ACK_WINDOW_OUT_OF_WINDOW) {
            return NET_RUDP_EMPTY;
        }
        if (wrc != NET_ACK_WINDOW_OK) {
            return NET_RUDP_ERR_PROTOCOL;
        }
    }

    if (payload_size > out_payload_capacity) {
        return NET_RUDP_ERR_SHORT;
    }

    const uint8_t *payload = frame_view.payload;

    /* Fragmented payloads are reassembled into a contiguous buffer and only
       delivered once complete.
     */
    if (flags & NET_RUDP_FLAG_FRAGMENT) {
        if (!peer->frag_enabled || !peer->reasm_buf || peer->reasm_buf_cap == 0u) {
            return NET_RUDP_ERR_INVALID;
        }
        if (payload_size < NET_RUDP_FRAG_HDR_SIZE) {
            return NET_RUDP_ERR_PROTOCOL;
        }

        uint16_t msg_id = read_u16_be(payload + 0);
        uint16_t frag_idx = read_u16_be(payload + 2);
        uint16_t frag_count = read_u16_be(payload + 4);
        uint16_t total_size = read_u16_be(payload + 6);

        if (frag_count == 0u || frag_count > NET_RUDP_FRAG_MAX) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        if (frag_idx >= frag_count) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        if (total_size == 0u) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        if ((size_t)total_size > peer->reasm_buf_cap || (size_t)total_size > out_payload_capacity) {
            return NET_RUDP_ERR_SHORT;
        }

        const size_t max_single = (NET_RUDP_MAX_PACKET_SIZE - NET_PACKET_HEADER_SIZE - NET_RUDP_FRAME_SIZE);
        if (max_single <= NET_RUDP_FRAG_HDR_SIZE) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        const size_t max_chunk = max_single - NET_RUDP_FRAG_HDR_SIZE;
        const size_t offset = (size_t)frag_idx * max_chunk;
        if (offset >= (size_t)total_size) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        size_t chunk_size = payload_size - NET_RUDP_FRAG_HDR_SIZE;
        if (chunk_size > (size_t)total_size - offset) {
            chunk_size = (size_t)total_size - offset;
        }

        /* Start/replace in-progress reassembly when msg_id changes. */
        if (peer->reasm_msg_id != msg_id || peer->reasm_frag_count != frag_count || peer->reasm_total_size != total_size || peer->reasm_schema_id != schema_id) {
            peer->reasm_msg_id = msg_id;
            peer->reasm_schema_id = schema_id;
            peer->reasm_total_size = total_size;
            peer->reasm_frag_count = frag_count;
            peer->reasm_frag_mask = 0u;
        }

        if (chunk_size > 0u) {
            memcpy(peer->reasm_buf + offset, payload + NET_RUDP_FRAG_HDR_SIZE, chunk_size);
        }
        peer->reasm_frag_mask |= ((uint64_t)1u << (uint64_t)frag_idx);

        const uint64_t want = mask_for_count_(peer->reasm_frag_count);
        if ((peer->reasm_frag_mask & want) != want) {
            return NET_RUDP_EMPTY;
        }

        memcpy(out_payload, peer->reasm_buf, (size_t)peer->reasm_total_size);
        *out_payload_size = (size_t)peer->reasm_total_size;
        *out_schema_id = peer->reasm_schema_id;
        *out_reliable = (uint8_t)((flags & NET_RUDP_FLAG_RELIABLE) != 0u);

        /* Reset reassembly state. */
        peer->reasm_msg_id = 0u;
        peer->reasm_schema_id = 0u;
        peer->reasm_total_size = 0u;
        peer->reasm_frag_count = 0u;
        peer->reasm_frag_mask = 0u;
        return NET_RUDP_OK;
    }

    memcpy(out_payload, payload, payload_size);
    *out_payload_size = payload_size;
    *out_schema_id = schema_id;
    *out_reliable = (uint8_t)((flags & NET_RUDP_FLAG_RELIABLE) != 0u);
    return NET_RUDP_OK;
}
