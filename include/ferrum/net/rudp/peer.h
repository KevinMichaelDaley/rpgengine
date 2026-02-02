#ifndef FERRUM_NET_RUDP_PEER_H
#define FERRUM_NET_RUDP_PEER_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/ack_window.h"
#include "ferrum/net/udp_socket.h"

/** @file
 * @brief Minimal reliable UDP peer state (ACK + resend for reliable packets).
 */

#ifdef __cplusplus
extern "C" {
#endif

#define NET_RUDP_OK 0
#define NET_RUDP_EMPTY 1

#define NET_RUDP_ERR_INVALID -1
#define NET_RUDP_ERR_SHORT -2
#define NET_RUDP_ERR_PROTOCOL -3
#define NET_RUDP_ERR_FULL -4

/** Fixed protocol id for p008 server/client integration. */
#define NET_RUDP_PROTOCOL_ID_P008 0x52555038u /* 'RUP8' */

/** Maximum encoded UDP packet size produced by this module. */
#define NET_RUDP_MAX_PACKET_SIZE 512u
/** Number of reliable send slots tracked per peer. */
#define NET_RUDP_SEND_SLOTS 64u

/** Per-peer RUDP state. */
typedef struct net_rudp_peer {
    uint32_t protocol_id;
    uint16_t next_sequence;
    net_ack_window_t recv_window;
    uint32_t resend_interval_ms;

    uint16_t slot_sequence[NET_RUDP_SEND_SLOTS];
    uint64_t slot_last_send_ms[NET_RUDP_SEND_SLOTS];
    uint16_t slot_size[NET_RUDP_SEND_SLOTS];
    uint8_t slot_used[NET_RUDP_SEND_SLOTS];
    uint8_t slot_packet_bytes[NET_RUDP_SEND_SLOTS][NET_RUDP_MAX_PACKET_SIZE];
} net_rudp_peer_t;

void net_rudp_peer_init(net_rudp_peer_t *peer, uint32_t protocol_id, uint32_t resend_interval_ms);

/**
 * @brief Process an incoming packet: validate, update recv window, and retire ACKed send slots.
 */
int net_rudp_peer_receive(net_rudp_peer_t *peer,
                          const uint8_t *packet,
                          size_t packet_size,
                          uint8_t *out_reliable,
                          uint16_t *out_schema_id,
                          uint8_t *out_payload,
                          size_t out_payload_capacity,
                          size_t *out_payload_size);

int net_rudp_peer_send_unreliable(net_rudp_peer_t *peer,
                                  net_udp_socket_t *sock,
                                  const net_udp_addr_t *to,
                                  uint64_t now_ms,
                                  uint16_t schema_id,
                                  const void *payload,
                                  size_t payload_size);

int net_rudp_peer_send_reliable(net_rudp_peer_t *peer,
                                net_udp_socket_t *sock,
                                const net_udp_addr_t *to,
                                uint64_t now_ms,
                                uint16_t schema_id,
                                const void *payload,
                                size_t payload_size,
                                uint16_t *out_sequence);

/**
 * @brief Resend any unacknowledged reliable packets past the resend interval.
 */
int net_rudp_peer_tick_resend(net_rudp_peer_t *peer,
                              net_udp_socket_t *sock,
                              const net_udp_addr_t *to,
                              uint64_t now_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_RUDP_PEER_H */
