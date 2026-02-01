#include "ferrum/net/udp_socket.h"

#include <errno.h>

#include <sys/socket.h>

int net_udp_socket_bind(net_udp_socket_t *sock, const net_udp_addr_t *local_addr) {
    if (!sock || !sock->initialized || !local_addr) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    if (local_addr->len == 0u || local_addr->len > sizeof(local_addr->storage)) {
        return NET_UDP_SOCKET_ERR_ADDR;
    }

    if (bind(sock->fd, (const struct sockaddr *)local_addr->storage, (socklen_t)local_addr->len) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}
