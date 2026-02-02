#ifndef FERRUM_NET_UDP_SOCKET_H
#define FERRUM_NET_UDP_SOCKET_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief POSIX UDP socket wrapper for Ferrum networking.
 *
 * Ownership: Caller owns all buffers. Socket owns its OS file descriptor.
 * Nullability: Public APIs validate pointers and return NET_UDP_SOCKET_ERR_INVALID.
 * Threading: Not thread-safe.
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_UDP_SOCKET_OK 0
#define NET_UDP_SOCKET_EMPTY 1
#define NET_UDP_SOCKET_TIMEOUT 2

#define NET_UDP_SOCKET_ERR_INVALID -1
#define NET_UDP_SOCKET_ERR_SYS -2
#define NET_UDP_SOCKET_ERR_ADDR -3

/** UDP socket handle. */
typedef struct net_udp_socket {
    int fd;
    uint8_t initialized;
} net_udp_socket_t;

/** UDP address (IPv4/IPv6). */
typedef struct net_udp_addr {
    uint8_t storage[128];
    uint32_t len;
} net_udp_addr_t;

/** @brief Opens a UDP socket (AF_INET/AF_INET6 agnostic). */
int net_udp_socket_open(net_udp_socket_t *sock);

/** @brief Closes the socket (idempotent, NULL-safe). */
void net_udp_socket_close(net_udp_socket_t *sock);

/** @brief Sets or clears nonblocking mode. */
int net_udp_socket_set_nonblocking(net_udp_socket_t *sock, int nonblocking);

/** @brief Sets SO_RCVTIMEO in milliseconds (0 disables timeout). */
int net_udp_socket_set_recv_timeout_ms(net_udp_socket_t *sock, uint32_t timeout_ms);

/** @brief Sets SO_RCVBUF (receive buffer size) in bytes.
 *
 * Best-effort: the OS may clamp the value.
 */
int net_udp_socket_set_recv_buffer_bytes(net_udp_socket_t *sock, uint32_t bytes);

/** @brief Sets SO_SNDBUF (send buffer size) in bytes.
 *
 * Best-effort: the OS may clamp the value.
 */
int net_udp_socket_set_send_buffer_bytes(net_udp_socket_t *sock, uint32_t bytes);

/** @brief Initializes an IPv4 address from octets and port. */
int net_udp_addr_ipv4(net_udp_addr_t *addr, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);

/** @brief Binds the socket to the given local address. */
int net_udp_socket_bind(net_udp_socket_t *sock, const net_udp_addr_t *local_addr);

/** @brief Gets the socket's local address (after bind). */
int net_udp_socket_local_addr(net_udp_socket_t *sock, net_udp_addr_t *out_local_addr);

/** @brief Sends a datagram to the destination address. */
int net_udp_socket_sendto(net_udp_socket_t *sock, const net_udp_addr_t *to, const void *data, size_t size);

/**
 * @brief Connects the UDP socket to a peer address.
 *
 * This does not establish a transport-level session; it only sets the default peer
 * for send/recv and filters received packets to that peer.
 */
int net_udp_socket_connect(net_udp_socket_t *sock, const net_udp_addr_t *peer);

/** @brief Sends a datagram to the connected peer (requires net_udp_socket_connect). */
int net_udp_socket_send(net_udp_socket_t *sock, const void *data, size_t size);

/**
 * @brief Receives a datagram from the connected peer.
 *
 * @return NET_UDP_SOCKET_OK on success,
 *         NET_UDP_SOCKET_EMPTY on nonblocking would-block,
 *         NET_UDP_SOCKET_TIMEOUT on blocking timeout.
 */
int net_udp_socket_recv(net_udp_socket_t *sock, void *out_data, size_t out_capacity, size_t *out_size);

/** @brief Receives a datagram.
 *
 * @return NET_UDP_SOCKET_OK on success,
 *         NET_UDP_SOCKET_EMPTY on nonblocking would-block,
 *         NET_UDP_SOCKET_TIMEOUT on blocking timeout.
 */
int net_udp_socket_recvfrom(net_udp_socket_t *sock,
                            net_udp_addr_t *out_from,
                            void *out_data,
                            size_t out_capacity,
                            size_t *out_size);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NET_UDP_SOCKET_H */
