#include "ferrum/net/udp_socket.h"

#include <fcntl.h>
#include <sys/socket.h>
#include <sys/time.h>

#include "internal.h"

static int net_udp_socket_is_nonblocking_(int fd, int *out_nonblocking) {
    if (!out_nonblocking) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    *out_nonblocking = (flags & O_NONBLOCK) ? 1 : 0;
    return NET_UDP_SOCKET_OK;
}

int net_udp_socket_set_nonblocking(net_udp_socket_t *sock, int nonblocking) {
    if (!sock || !sock->initialized) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    int flags = fcntl(sock->fd, F_GETFL, 0);
    if (flags < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    if (nonblocking) {
        flags |= O_NONBLOCK;
    } else {
        flags &= ~O_NONBLOCK;
    }

    if (fcntl(sock->fd, F_SETFL, flags) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

int net_udp_socket_set_recv_timeout_ms(net_udp_socket_t *sock, uint32_t timeout_ms) {
    if (!sock || !sock->initialized) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    struct timeval tv;
    tv.tv_sec = (time_t)(timeout_ms / 1000u);
    tv.tv_usec = (suseconds_t)((timeout_ms % 1000u) * 1000u);

    if (setsockopt(sock->fd, SOL_SOCKET, SO_RCVTIMEO, &tv, (socklen_t)sizeof(tv)) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

int net_udp_socket__internal_is_nonblocking(const net_udp_socket_t *sock, int *out_nonblocking) {
    if (!sock || !sock->initialized) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    return net_udp_socket_is_nonblocking_(sock->fd, out_nonblocking);
}
