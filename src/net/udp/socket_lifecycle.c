#include "ferrum/net/udp_socket.h"

#include <errno.h>
#include <string.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

int net_udp_socket_open(net_udp_socket_t *sock) {
    if (!sock) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    memset(sock, 0, sizeof(*sock));
    sock->fd = -1;

    int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    sock->fd = fd;
    sock->initialized = 1u;
    return NET_UDP_SOCKET_OK;
}

void net_udp_socket_close(net_udp_socket_t *sock) {
    if (!sock || !sock->initialized) {
        return;
    }

    if (sock->fd >= 0) {
        (void)close(sock->fd);
    }

    sock->fd = -1;
    sock->initialized = 0u;
}
