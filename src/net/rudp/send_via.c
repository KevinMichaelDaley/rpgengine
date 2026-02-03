#include "ferrum/net/rudp/peer.h"

#include "sendto_internal.h"

int net_rudp_peer_send_unreliable_via(net_rudp_peer_t *peer,
                                      void *io_user,
                                      int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                      const net_udp_addr_t *to,
                                      uint64_t now_ms,
                                      uint16_t schema_id,
                                      const void *payload,
                                      size_t payload_size) {
    return net_rudp_peer_send_unreliable_with_sendto(peer, io_user, sendto_cb, to, now_ms, schema_id, payload, payload_size);
}

int net_rudp_peer_send_reliable_via(net_rudp_peer_t *peer,
                                    void *io_user,
                                    int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                    const net_udp_addr_t *to,
                                    uint64_t now_ms,
                                    uint16_t schema_id,
                                    const void *payload,
                                    size_t payload_size,
                                    uint16_t *out_sequence) {
    return net_rudp_peer_send_reliable_with_sendto(peer,
                                                   io_user,
                                                   sendto_cb,
                                                   to,
                                                   now_ms,
                                                   schema_id,
                                                   payload,
                                                   payload_size,
                                                   out_sequence);
}

int net_rudp_peer_tick_resend_via(net_rudp_peer_t *peer,
                                  void *io_user,
                                  int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                  const net_udp_addr_t *to,
                                  uint64_t now_ms) {
    return net_rudp_peer_tick_resend_with_sendto(peer, io_user, sendto_cb, to, now_ms);
}
