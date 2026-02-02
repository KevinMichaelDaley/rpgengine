#include <string.h>

#include "ferrum/net/rudp/peer.h"

size_t net_rudp_send_slot_storage_size(size_t slot_count) {
    return slot_count * sizeof(net_rudp_send_slot_t);
}

void net_rudp_peer_init_with_storage(net_rudp_peer_t *peer,
                                     uint32_t protocol_id,
                                     uint32_t resend_interval_ms,
                                     net_rudp_send_slot_t *send_slots,
                                     size_t send_slot_count) {
    if (!peer) {
        return;
    }
    memset(peer, 0, sizeof(*peer));
    peer->protocol_id = protocol_id;
    peer->next_sequence = 0u;
    peer->resend_interval_ms = (resend_interval_ms == 0u) ? 50u : resend_interval_ms;
    net_ack_window_init(&peer->recv_window);

    peer->send_slots = send_slots;
    peer->send_slot_count = send_slot_count;

    if (peer->send_slots && peer->send_slot_count > 0u) {
        memset(peer->send_slots, 0, peer->send_slot_count * sizeof(*peer->send_slots));
    }
}
