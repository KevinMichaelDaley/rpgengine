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
        /* Start at 1 so ack=0 (uninitialized recv window) cannot accidentally
             ACK the first reliable packet.
         */
        peer->next_sequence = 1u;
    peer->resend_interval_ms = (resend_interval_ms == 0u) ? 50u : resend_interval_ms;
    peer->max_slot_age_ms = 5000u; /* 5 seconds default TTL for unACKed slots. */
    net_ack_window_init(&peer->recv_window);

    peer->send_slots = send_slots;
    peer->send_slot_count = send_slot_count;

    if (peer->send_slots && peer->send_slot_count > 0u) {
        memset(peer->send_slots, 0, peer->send_slot_count * sizeof(*peer->send_slots));
    }

    /* Enable fragmentation + reassembly by default.
       This keeps fragmentation opaque to callers and provides one buffer per peer.
     */
    peer->frag_enabled = 1u;
    peer->next_msg_id = 1u;
    peer->reasm_buf = peer->reasm_storage;
    peer->reasm_buf_cap = sizeof(peer->reasm_storage);
    peer->reasm_msg_id = 0u;
    peer->reasm_schema_id = 0u;
    peer->reasm_total_size = 0u;
    peer->reasm_frag_count = 0u;
    peer->reasm_frag_mask = 0u;
}

void net_rudp_peer_enable_fragmentation(net_rudp_peer_t *peer, int enabled, uint8_t *reasm_buf, size_t reasm_buf_cap) {
    if (!peer) {
        return;
    }

    peer->frag_enabled = (uint8_t)(enabled ? 1u : 0u);
    peer->next_msg_id = 1u;

    if (peer->frag_enabled) {
        /* If caller doesn't provide a buffer, fall back to internal storage. */
        if (!reasm_buf || reasm_buf_cap == 0u) {
            peer->reasm_buf = peer->reasm_storage;
            peer->reasm_buf_cap = sizeof(peer->reasm_storage);
        } else {
            peer->reasm_buf = reasm_buf;
            peer->reasm_buf_cap = reasm_buf_cap;
        }
    } else {
        peer->reasm_buf = NULL;
        peer->reasm_buf_cap = 0u;
    }
    peer->reasm_msg_id = 0u;
    peer->reasm_schema_id = 0u;
    peer->reasm_total_size = 0u;
    peer->reasm_frag_count = 0u;
    peer->reasm_frag_mask = 0u;
}
