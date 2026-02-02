#include <string.h>

#include "ferrum/net/rudp/peer.h"

void net_rudp_peer_init(net_rudp_peer_t *peer, uint32_t protocol_id, uint32_t resend_interval_ms) {
    if (!peer) {
        return;
    }
    memset(peer, 0, sizeof(*peer));
    peer->protocol_id = protocol_id;
    peer->next_sequence = 0u;
    peer->resend_interval_ms = (resend_interval_ms == 0u) ? 50u : resend_interval_ms;
    net_ack_window_init(&peer->recv_window);
}
