#include "ferrum/net/udp_socket.h"

#include "internal.h"

#include <errno.h>

#include <sys/socket.h>

int net_udp_socket_connect(net_udp_socket_t *sock, const net_udp_addr_t *peer) {
    if (!sock || !sock->initialized || !peer) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    if (peer->len == 0u || peer->len > sizeof(peer->storage)) {
        return NET_UDP_SOCKET_ERR_ADDR;
    }

    if (connect(sock->fd, (const struct sockaddr *)peer->storage, (socklen_t)peer->len) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

int net_udp_socket_send(net_udp_socket_t *sock, const void *data, size_t size) {
    if (!sock || !sock->initialized || (!data && size != 0u)) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    ssize_t rc = send(sock->fd, data, size, 0);
    if (rc < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

int net_udp_socket_recv(net_udp_socket_t *sock, void *out_data, size_t out_capacity, size_t *out_size) {
    if (!sock || !sock->initialized || !out_data || !out_size) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    ssize_t rc = recv(sock->fd, out_data, out_capacity, 0);
    if (rc < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            int nonblocking = 0;
            int nb_rc = net_udp_socket__internal_is_nonblocking(sock, &nonblocking);
            if (nb_rc != NET_UDP_SOCKET_OK) {
                return nb_rc;
            }

            if (nonblocking) {
                return NET_UDP_SOCKET_EMPTY;
            }
            return NET_UDP_SOCKET_TIMEOUT;
        }
        return NET_UDP_SOCKET_ERR_SYS;
    }

    if ((size_t)rc > out_capacity) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    *out_size = (size_t)rc;
    return NET_UDP_SOCKET_OK;
}
