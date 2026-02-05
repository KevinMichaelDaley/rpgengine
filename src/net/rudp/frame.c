#include "ferrum/net/packet_header.h"
#include "ferrum/net/rudp/peer.h"
#include "ferrum/net/rudp/reliability.h"
#include "ferrum/net/rudp/wire_frame.h"

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

    return net_rudp_reliability_receive(peer, &header, &frame_view, out_reliable, out_schema_id, out_payload, out_payload_capacity,
                                        out_payload_size);
}
