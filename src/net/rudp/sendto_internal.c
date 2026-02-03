#include <string.h>

#include "ferrum/net/packet_header.h"

#include "sendto_internal.h"

#define NET_RUDP_FRAME_SIZE 8u

#define NET_RUDP_FLAG_RELIABLE 0x01u
#define NET_RUDP_FLAG_FRAGMENT 0x02u

#define NET_RUDP_FRAG_HDR_SIZE 8u
#define NET_RUDP_FRAG_MAX 64u

static void write_u16_be_(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xFFu);
    out[1] = (uint8_t)(v & 0xFFu);
}

static int build_packet_(net_rudp_peer_t *peer,
                         uint64_t now_ms,
                         uint8_t flags,
                         uint16_t schema_id,
                         const void *payload,
                         size_t payload_size,
                         uint8_t *out_packet,
                         size_t out_capacity,
                         size_t *out_size,
                         uint16_t *out_sequence) {
    if (!peer || !payload || !out_packet || !out_size) {
        return NET_RUDP_ERR_INVALID;
    }
    if (payload_size > (NET_RUDP_MAX_PACKET_SIZE - NET_PACKET_HEADER_SIZE - NET_RUDP_FRAME_SIZE)) {
        return NET_RUDP_ERR_INVALID;
    }
    if (out_capacity < NET_PACKET_HEADER_SIZE + NET_RUDP_FRAME_SIZE + payload_size) {
        return NET_RUDP_ERR_SHORT;
    }

    net_packet_header_t header;
    header.protocol_id = peer->protocol_id;
    header.sequence = peer->next_sequence;
    header.ack = net_ack_window_ack(&peer->recv_window);
    header.ack_bits = net_ack_window_ack_bits(&peer->recv_window);

    (void)now_ms;

    int rc = net_packet_header_encode(&header, out_packet, out_capacity);
    if (rc != NET_PACKET_HEADER_OK) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    uint8_t *frame = out_packet + NET_PACKET_HEADER_SIZE;
    frame[0] = flags;
    frame[1] = 0u;
    write_u16_be_(frame + 2, schema_id);
    write_u16_be_(frame + 4, (uint16_t)payload_size);
    write_u16_be_(frame + 6, 0u);
    memcpy(frame + NET_RUDP_FRAME_SIZE, payload, payload_size);

    *out_size = NET_PACKET_HEADER_SIZE + NET_RUDP_FRAME_SIZE + payload_size;
    if (out_sequence) {
        *out_sequence = header.sequence;
    }
    /* Only advance the sequence for reliable packets.
       Reliable receive uses a bounded ACK/duplicate window; if unreliable traffic
       also advances the sequence, reliable packets can become permanently
       out-of-window under high-rate unreliable sends.
     */
    if (flags & NET_RUDP_FLAG_RELIABLE) {
        peer->next_sequence = (uint16_t)(peer->next_sequence + 1u);
    }
    return NET_RUDP_OK;
}

static size_t max_single_payload_(void) {
    return (size_t)(NET_RUDP_MAX_PACKET_SIZE - NET_PACKET_HEADER_SIZE - NET_RUDP_FRAME_SIZE);
}

int net_rudp_peer_send_unreliable_with_sendto(net_rudp_peer_t *peer,
                                              void *io_user,
                                              int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                              const net_udp_addr_t *to,
                                              uint64_t now_ms,
                                              uint16_t schema_id,
                                              const void *payload,
                                              size_t payload_size) {
    if (!peer || !sendto_cb || !to || !payload) {
        return NET_RUDP_ERR_INVALID;
    }

    const size_t max_single = max_single_payload_();
    if (payload_size <= max_single) {
        uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
        size_t packet_size = 0u;
        int rc = build_packet_(peer, now_ms, 0u, schema_id, payload, payload_size, packet, sizeof(packet), &packet_size, NULL);
        if (rc != NET_RUDP_OK) {
            return rc;
        }
        return (sendto_cb(io_user, to, packet, packet_size) == 0) ? NET_RUDP_OK : NET_RUDP_ERR_PROTOCOL;
    }

    if (!peer->frag_enabled) {
        return NET_RUDP_ERR_INVALID;
    }
    if (max_single <= NET_RUDP_FRAG_HDR_SIZE) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    const size_t max_chunk = max_single - NET_RUDP_FRAG_HDR_SIZE;
    const uint16_t frag_count = (uint16_t)((payload_size + max_chunk - 1u) / max_chunk);
    if (frag_count == 0u || frag_count > NET_RUDP_FRAG_MAX) {
        return NET_RUDP_ERR_INVALID;
    }

    uint16_t msg_id = peer->next_msg_id;
    peer->next_msg_id = (uint16_t)(peer->next_msg_id + 1u);
    if (peer->next_msg_id == 0u) {
        peer->next_msg_id = 1u;
    }

    for (uint16_t frag_idx = 0u; frag_idx < frag_count; ++frag_idx) {
        const size_t offset = (size_t)frag_idx * max_chunk;
        size_t chunk = payload_size - offset;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        }

        uint8_t frag_payload[NET_RUDP_MAX_PACKET_SIZE];
        write_u16_be_(frag_payload + 0, msg_id);
        write_u16_be_(frag_payload + 2, frag_idx);
        write_u16_be_(frag_payload + 4, frag_count);
        write_u16_be_(frag_payload + 6, (uint16_t)payload_size);
        memcpy(frag_payload + NET_RUDP_FRAG_HDR_SIZE, (const uint8_t *)payload + offset, chunk);

        uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
        size_t packet_size = 0u;
        int rc = build_packet_(peer,
                               now_ms,
                               (uint8_t)(NET_RUDP_FLAG_FRAGMENT),
                               schema_id,
                               frag_payload,
                               NET_RUDP_FRAG_HDR_SIZE + chunk,
                               packet,
                               sizeof(packet),
                               &packet_size,
                               NULL);
        if (rc != NET_RUDP_OK) {
            return rc;
        }
        if (sendto_cb(io_user, to, packet, packet_size) != 0) {
            return NET_RUDP_ERR_PROTOCOL;
        }
    }

    return NET_RUDP_OK;
}

int net_rudp_peer_send_reliable_with_sendto(net_rudp_peer_t *peer,
                                            void *io_user,
                                            int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                            const net_udp_addr_t *to,
                                            uint64_t now_ms,
                                            uint16_t schema_id,
                                            const void *payload,
                                            size_t payload_size,
                                            uint16_t *out_sequence) {
    if (!peer || !sendto_cb || !to || !payload) {
        return NET_RUDP_ERR_INVALID;
    }
    if (!peer->send_slots || peer->send_slot_count == 0u) {
        return NET_RUDP_ERR_FULL;
    }

    const size_t max_single = max_single_payload_();

    /* Fast path: no fragmentation. */
    if (payload_size <= max_single) {
        size_t slot = peer->send_slot_count;
        for (size_t i = 0u; i < peer->send_slot_count; ++i) {
            if (!peer->send_slots[i].used) {
                slot = i;
                break;
            }
        }
        if (slot == peer->send_slot_count) {
            return NET_RUDP_ERR_FULL;
        }

        size_t packet_size = 0u;
        uint16_t sequence = 0u;
        int rc = build_packet_(peer,
                               now_ms,
                               NET_RUDP_FLAG_RELIABLE,
                               schema_id,
                               payload,
                               payload_size,
                               peer->send_slots[slot].packet_bytes,
                               NET_RUDP_MAX_PACKET_SIZE,
                               &packet_size,
                               &sequence);
        if (rc != NET_RUDP_OK) {
            return rc;
        }

        peer->send_slots[slot].used = 1u;
        peer->send_slots[slot].sequence = sequence;
        peer->send_slots[slot].size = (uint16_t)packet_size;
        peer->send_slots[slot].last_send_ms = now_ms;

        if (sendto_cb(io_user, to, peer->send_slots[slot].packet_bytes, packet_size) != 0) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        if (out_sequence) {
            *out_sequence = sequence;
        }
        return NET_RUDP_OK;
    }

    if (!peer->frag_enabled) {
        return NET_RUDP_ERR_INVALID;
    }
    if (max_single <= NET_RUDP_FRAG_HDR_SIZE) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    const size_t max_chunk = max_single - NET_RUDP_FRAG_HDR_SIZE;
    const uint16_t frag_count = (uint16_t)((payload_size + max_chunk - 1u) / max_chunk);
    if (frag_count == 0u || frag_count > NET_RUDP_FRAG_MAX) {
        return NET_RUDP_ERR_INVALID;
    }

    uint16_t msg_id = peer->next_msg_id;
    peer->next_msg_id = (uint16_t)(peer->next_msg_id + 1u);
    if (peer->next_msg_id == 0u) {
        peer->next_msg_id = 1u;
    }

    uint16_t first_seq = 0u;
    int first_seq_set = 0;

    for (uint16_t frag_idx = 0u; frag_idx < frag_count; ++frag_idx) {
        size_t slot = peer->send_slot_count;
        for (size_t i = 0u; i < peer->send_slot_count; ++i) {
            if (!peer->send_slots[i].used) {
                slot = i;
                break;
            }
        }
        if (slot == peer->send_slot_count) {
            return NET_RUDP_ERR_FULL;
        }

        const size_t offset = (size_t)frag_idx * max_chunk;
        size_t chunk = payload_size - offset;
        if (chunk > max_chunk) {
            chunk = max_chunk;
        }

        uint8_t frag_payload[NET_RUDP_MAX_PACKET_SIZE];
        write_u16_be_(frag_payload + 0, msg_id);
        write_u16_be_(frag_payload + 2, frag_idx);
        write_u16_be_(frag_payload + 4, frag_count);
        write_u16_be_(frag_payload + 6, (uint16_t)payload_size);
        memcpy(frag_payload + NET_RUDP_FRAG_HDR_SIZE, (const uint8_t *)payload + offset, chunk);

        size_t packet_size = 0u;
        uint16_t sequence = 0u;
        int rc = build_packet_(peer,
                               now_ms,
                               (uint8_t)(NET_RUDP_FLAG_RELIABLE | NET_RUDP_FLAG_FRAGMENT),
                               schema_id,
                               frag_payload,
                               NET_RUDP_FRAG_HDR_SIZE + chunk,
                               peer->send_slots[slot].packet_bytes,
                               NET_RUDP_MAX_PACKET_SIZE,
                               &packet_size,
                               &sequence);
        if (rc != NET_RUDP_OK) {
            return rc;
        }

        peer->send_slots[slot].used = 1u;
        peer->send_slots[slot].sequence = sequence;
        peer->send_slots[slot].size = (uint16_t)packet_size;
        peer->send_slots[slot].last_send_ms = now_ms;

        if (sendto_cb(io_user, to, peer->send_slots[slot].packet_bytes, packet_size) != 0) {
            return NET_RUDP_ERR_PROTOCOL;
        }
        if (!first_seq_set) {
            first_seq = sequence;
            first_seq_set = 1;
        }
    }

    if (out_sequence && first_seq_set) {
        *out_sequence = first_seq;
    }
    return NET_RUDP_OK;
}

int net_rudp_peer_tick_resend_with_sendto(net_rudp_peer_t *peer,
                                          void *io_user,
                                          int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                          const net_udp_addr_t *to,
                                          uint64_t now_ms) {
    if (!peer || !sendto_cb || !to) {
        return NET_RUDP_ERR_INVALID;
    }

    if (!peer->send_slots || peer->send_slot_count == 0u) {
        return NET_RUDP_OK;
    }

    for (size_t i = 0u; i < peer->send_slot_count; ++i) {
        if (!peer->send_slots[i].used) {
            continue;
        }
        uint64_t elapsed = now_ms - peer->send_slots[i].last_send_ms;
        if (elapsed < (uint64_t)peer->resend_interval_ms) {
            continue;
        }
        peer->send_slots[i].last_send_ms = now_ms;
        (void)sendto_cb(io_user, to, peer->send_slots[i].packet_bytes, (size_t)peer->send_slots[i].size);
    }

    return NET_RUDP_OK;
}
