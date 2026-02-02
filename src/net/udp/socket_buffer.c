#include "ferrum/net/udp_socket.h"

#include <sys/socket.h>

#include "internal.h"

int net_udp_socket_set_recv_buffer_bytes(net_udp_socket_t *sock, uint32_t bytes) {
    if (!sock || !sock->initialized || bytes == 0u) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    int v = (int)bytes;
    if (v <= 0) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVBUF, &v, (socklen_t)sizeof(v)) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

int net_udp_socket_set_send_buffer_bytes(net_udp_socket_t *sock, uint32_t bytes) {
    if (!sock || !sock->initialized || bytes == 0u) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    int v = (int)bytes;
    if (v <= 0) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    if (setsockopt(sock->fd, SOL_SOCKET, SO_SNDBUF, &v, (socklen_t)sizeof(v)) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}
