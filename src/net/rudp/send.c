#include "ferrum/net/udp_socket.h"
#include "ferrum/net/rudp/reliability_send.h"

static int socket_sendto_(void *io_user, const net_udp_addr_t *to, const void *data, size_t size) {
    net_udp_socket_t *sock = (net_udp_socket_t *)io_user;
    if (!sock || !to || !data) {
        return -1;
    }
    return (net_udp_socket_sendto(sock, to, data, size) == NET_UDP_SOCKET_OK) ? 0 : -1;
}

int net_rudp_peer_send_unreliable(net_rudp_peer_t *peer,
                                  net_udp_socket_t *sock,
                                  const net_udp_addr_t *to,
                                  uint64_t now_ms,
                                  uint16_t schema_id,
                                  const void *payload,
                                  size_t payload_size) {
    return net_rudp_reliability_send_unreliable_via(peer, sock, socket_sendto_, to, now_ms, schema_id, payload, payload_size);
}

int net_rudp_peer_send_reliable(net_rudp_peer_t *peer,
                                net_udp_socket_t *sock,
                                const net_udp_addr_t *to,
                                uint64_t now_ms,
                                uint16_t schema_id,
                                const void *payload,
                                size_t payload_size,
                                uint16_t *out_sequence) {
    return net_rudp_reliability_send_reliable_via(peer, sock, socket_sendto_, to, now_ms, schema_id, payload, payload_size, out_sequence);
}

int net_rudp_peer_tick_resend(net_rudp_peer_t *peer, net_udp_socket_t *sock, const net_udp_addr_t *to, uint64_t now_ms) {
    return net_rudp_reliability_tick_resend_via(peer, sock, socket_sendto_, to, now_ms);
}
