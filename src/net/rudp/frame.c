#include <string.h>

#include "ferrum/net/packet_header.h"
#include "ferrum/net/rudp/peer.h"

#define NET_RUDP_FRAME_SIZE 8u

#define NET_RUDP_FLAG_RELIABLE 0x01u

static void write_u16_be(uint8_t *out, uint16_t v) {
    out[0] = (uint8_t)((v >> 8) & 0xFFu);
    out[1] = (uint8_t)(v & 0xFFu);
}

static uint16_t read_u16_be(const uint8_t *in) {
    return (uint16_t)(((uint16_t)in[0] << 8) | (uint16_t)in[1]);
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
    if (packet_size < NET_PACKET_HEADER_SIZE + NET_RUDP_FRAME_SIZE) {
        return NET_RUDP_ERR_SHORT;
    }

    net_packet_header_t header;
    int rc = net_packet_header_decode(&header, packet, packet_size);
    if (rc != NET_PACKET_HEADER_OK) {
        return NET_RUDP_ERR_PROTOCOL;
    }
    if (header.protocol_id != peer->protocol_id) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    /* Retire ACKed reliable sends. */
    uint16_t ack = header.ack;
    uint32_t ack_bits = header.ack_bits;
    for (size_t i = 0u; i < NET_RUDP_SEND_SLOTS; ++i) {
        if (!peer->slot_used[i]) {
            continue;
        }
        uint16_t seq = peer->slot_sequence[i];
        if (seq == ack) {
            peer->slot_used[i] = 0u;
            continue;
        }
        uint16_t delta = (uint16_t)(ack - seq);
        if (delta >= 1u && delta <= 32u) {
            uint32_t bit = 1u << (uint32_t)(delta - 1u);
            if (ack_bits & bit) {
                peer->slot_used[i] = 0u;
            }
        }
    }

    /* Update receive window and drop duplicates/old. */
    int wrc = net_ack_window_receive(&peer->recv_window, header.sequence);
    if (wrc == NET_ACK_WINDOW_DUPLICATE || wrc == NET_ACK_WINDOW_OUT_OF_WINDOW) {
        return NET_RUDP_EMPTY;
    }
    if (wrc != NET_ACK_WINDOW_OK) {
        return NET_RUDP_ERR_PROTOCOL;
    }

    const uint8_t *frame = packet + NET_PACKET_HEADER_SIZE;
    uint8_t flags = frame[0];
    (void)frame[1];
    uint16_t schema_id = read_u16_be(frame + 2);
    uint16_t payload_size = read_u16_be(frame + 4);
    (void)read_u16_be(frame + 6);

    const size_t total_needed = NET_PACKET_HEADER_SIZE + NET_RUDP_FRAME_SIZE + (size_t)payload_size;
    if (packet_size < total_needed) {
        return NET_RUDP_ERR_SHORT;
    }
    if ((size_t)payload_size > out_payload_capacity) {
        return NET_RUDP_ERR_SHORT;
    }

    memcpy(out_payload, frame + NET_RUDP_FRAME_SIZE, (size_t)payload_size);
    *out_payload_size = (size_t)payload_size;
    *out_schema_id = schema_id;
    *out_reliable = (uint8_t)((flags & NET_RUDP_FLAG_RELIABLE) != 0u);
    return NET_RUDP_OK;
}

static int build_packet(net_rudp_peer_t *peer,
                        uint64_t now_ms,
                        uint8_t reliable,
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
    frame[0] = reliable ? NET_RUDP_FLAG_RELIABLE : 0u;
    frame[1] = 0u;
    write_u16_be(frame + 2, schema_id);
    write_u16_be(frame + 4, (uint16_t)payload_size);
    write_u16_be(frame + 6, 0u);
    memcpy(frame + NET_RUDP_FRAME_SIZE, payload, payload_size);

    *out_size = NET_PACKET_HEADER_SIZE + NET_RUDP_FRAME_SIZE + payload_size;
    if (out_sequence) {
        *out_sequence = header.sequence;
    }
    peer->next_sequence = (uint16_t)(peer->next_sequence + 1u);
    return NET_RUDP_OK;
}

int net_rudp_peer_send_unreliable(net_rudp_peer_t *peer,
                                  net_udp_socket_t *sock,
                                  const net_udp_addr_t *to,
                                  uint64_t now_ms,
                                  uint16_t schema_id,
                                  const void *payload,
                                  size_t payload_size) {
    if (!peer || !sock || !to || !payload) {
        return NET_RUDP_ERR_INVALID;
    }

    uint8_t packet[NET_RUDP_MAX_PACKET_SIZE];
    size_t packet_size = 0u;
    int rc = build_packet(peer, now_ms, 0u, schema_id, payload, payload_size, packet, sizeof(packet), &packet_size, NULL);
    if (rc != NET_RUDP_OK) {
        return rc;
    }
    int src = net_udp_socket_sendto(sock, to, packet, packet_size);
    return (src == NET_UDP_SOCKET_OK) ? NET_RUDP_OK : NET_RUDP_ERR_PROTOCOL;
}

int net_rudp_peer_send_reliable(net_rudp_peer_t *peer,
                                net_udp_socket_t *sock,
                                const net_udp_addr_t *to,
                                uint64_t now_ms,
                                uint16_t schema_id,
                                const void *payload,
                                size_t payload_size,
                                uint16_t *out_sequence) {
    if (!peer || !sock || !to || !payload) {
        return NET_RUDP_ERR_INVALID;
    }

    size_t slot = NET_RUDP_SEND_SLOTS;
    for (size_t i = 0u; i < NET_RUDP_SEND_SLOTS; ++i) {
        if (!peer->slot_used[i]) {
            slot = i;
            break;
        }
    }
    if (slot == NET_RUDP_SEND_SLOTS) {
        return NET_RUDP_ERR_FULL;
    }

    size_t packet_size = 0u;
    uint16_t sequence = 0u;
    int rc = build_packet(peer,
                          now_ms,
                          1u,
                          schema_id,
                          payload,
                          payload_size,
                          peer->slot_packet_bytes[slot],
                          NET_RUDP_MAX_PACKET_SIZE,
                          &packet_size,
                          &sequence);
    if (rc != NET_RUDP_OK) {
        return rc;
    }

    peer->slot_used[slot] = 1u;
    peer->slot_sequence[slot] = sequence;
    peer->slot_size[slot] = (uint16_t)packet_size;
    peer->slot_last_send_ms[slot] = now_ms;

    int src = net_udp_socket_sendto(sock, to, peer->slot_packet_bytes[slot], packet_size);
    if (src != NET_UDP_SOCKET_OK) {
        return NET_RUDP_ERR_PROTOCOL;
    }
    if (out_sequence) {
        *out_sequence = sequence;
    }
    return NET_RUDP_OK;
}

int net_rudp_peer_tick_resend(net_rudp_peer_t *peer,
                              net_udp_socket_t *sock,
                              const net_udp_addr_t *to,
                              uint64_t now_ms) {
    if (!peer || !sock || !to) {
        return NET_RUDP_ERR_INVALID;
    }

    for (size_t i = 0u; i < NET_RUDP_SEND_SLOTS; ++i) {
        if (!peer->slot_used[i]) {
            continue;
        }
        uint64_t elapsed = now_ms - peer->slot_last_send_ms[i];
        if (elapsed < (uint64_t)peer->resend_interval_ms) {
            continue;
        }
        peer->slot_last_send_ms[i] = now_ms;
        (void)net_udp_socket_sendto(sock, to, peer->slot_packet_bytes[i], (size_t)peer->slot_size[i]);
    }

    return NET_RUDP_OK;
}
