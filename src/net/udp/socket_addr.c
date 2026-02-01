#include "ferrum/net/udp_socket.h"

#include <errno.h>
#include <string.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

static int net_udp_addr_write_(net_udp_addr_t *dst, const void *src, size_t len) {
    if (!dst || !src) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }
    if (len > sizeof(dst->storage)) {
        return NET_UDP_SOCKET_ERR_ADDR;
    }

    memcpy(dst->storage, src, len);
    dst->len = (uint32_t)len;
    return NET_UDP_SOCKET_OK;
}

int net_udp_addr_ipv4(net_udp_addr_t *addr, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
    if (!addr) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_port = htons(port);
    sa.sin_addr.s_addr = htonl(((uint32_t)a << 24u) | ((uint32_t)b << 16u) | ((uint32_t)c << 8u) | (uint32_t)d);

    return net_udp_addr_write_(addr, &sa, sizeof(sa));
}

int net_udp_socket_local_addr(net_udp_socket_t *sock, net_udp_addr_t *out_local_addr) {
    if (!sock || !sock->initialized || !out_local_addr) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    struct sockaddr_storage ss;
    socklen_t len = (socklen_t)sizeof(ss);

    if (getsockname(sock->fd, (struct sockaddr *)&ss, &len) < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return net_udp_addr_write_(out_local_addr, &ss, (size_t)len);
}
