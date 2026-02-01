#ifndef FERRUM_NET_UDP_INTERNAL_H
#define FERRUM_NET_UDP_INTERNAL_H

#include "ferrum/net/udp_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

int net_udp_socket__internal_is_nonblocking(const net_udp_socket_t *sock, int *out_nonblocking);

#ifdef __cplusplus
}
#endif

#endif
