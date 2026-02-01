#include "ferrum/net/udp_socket.h"

#include "internal.h"

#include <errno.h>
#include <string.h>

#include <sys/socket.h>

int net_udp_socket_sendto(net_udp_socket_t *sock, const net_udp_addr_t *to, const void *data, size_t size) {
    if (!sock || !sock->initialized || !to || (!data && size != 0u)) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    if (to->len == 0u || to->len > sizeof(to->storage)) {
        return NET_UDP_SOCKET_ERR_ADDR;
    }

    ssize_t rc = sendto(sock->fd, data, size, 0, (const struct sockaddr *)to->storage, (socklen_t)to->len);
    if (rc < 0) {
        return NET_UDP_SOCKET_ERR_SYS;
    }

    return NET_UDP_SOCKET_OK;
}

int net_udp_socket_recvfrom(net_udp_socket_t *sock,
                            net_udp_addr_t *out_from,
                            void *out_data,
                            size_t out_capacity,
                            size_t *out_size) {
    if (!sock || !sock->initialized || !out_from || !out_data || !out_size) {
        return NET_UDP_SOCKET_ERR_INVALID;
    }

    struct sockaddr_storage ss;
    socklen_t len = (socklen_t)sizeof(ss);

    ssize_t rc = recvfrom(sock->fd, out_data, out_capacity, 0, (struct sockaddr *)&ss, &len);
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

    if ((size_t)len > sizeof(out_from->storage)) {
        return NET_UDP_SOCKET_ERR_ADDR;
    }

    memcpy(out_from->storage, &ss, (size_t)len);
    out_from->len = (uint32_t)len;

    *out_size = (size_t)rc;
    return NET_UDP_SOCKET_OK;
}
