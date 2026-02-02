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

/** Default number of reliable send slots per peer (legacy sizing). */
#define NET_RUDP_SEND_SLOTS_DEFAULT 64u

/** One tracked reliable send slot. Caller owns storage lifetime. */
typedef struct net_rudp_send_slot {
    uint16_t sequence;
    uint64_t last_send_ms;
    uint16_t size;
    uint8_t used;
    uint8_t packet_bytes[NET_RUDP_MAX_PACKET_SIZE];
} net_rudp_send_slot_t;

/** Per-peer RUDP state. */
typedef struct net_rudp_peer {
    uint32_t protocol_id;
    uint16_t next_sequence;
    net_ack_window_t recv_window;
    uint32_t resend_interval_ms;

    net_rudp_send_slot_t *send_slots;
    size_t send_slot_count;
} net_rudp_peer_t;

/** Returns required size (bytes) for an array of `net_rudp_send_slot_t` of length `slot_count`. */
size_t net_rudp_send_slot_storage_size(size_t slot_count);

/** Initialize a peer with caller-provided send-slot storage. */
void net_rudp_peer_init_with_storage(net_rudp_peer_t *peer,
                                     uint32_t protocol_id,
                                     uint32_t resend_interval_ms,
                                     net_rudp_send_slot_t *send_slots,
                                     size_t send_slot_count);

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
