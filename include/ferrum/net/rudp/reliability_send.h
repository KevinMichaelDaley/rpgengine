#ifndef FERRUM_NET_RUDP_RELIABILITY_SEND_H
#define FERRUM_NET_RUDP_RELIABILITY_SEND_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/rudp/peer.h"

/** @file
 * @brief Reliability-layer send helpers for RUDP.
 *
 * This layer is responsible for:
 * - Building packets for transmission (including ACK header state).
 * - Tracking reliable send slots for retransmission.
 * - Retrying unacked reliable packets on a resend interval.
 * - Optional payload fragmentation for oversized messages.
 *
 * Wire framing is handled via the minimal wire framing module.
 */

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Send an unreliable message using a caller-provided sendto callback.
 */
int net_rudp_reliability_send_unreliable_via(net_rudp_peer_t *peer,
                                            void *io_user,
                                            int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                            const net_udp_addr_t *to,
                                            uint64_t now_ms,
                                            uint16_t schema_id,
                                            const void *payload,
                                            size_t payload_size);

/**
 * @brief Send a reliable message using a caller-provided sendto callback.
 */
int net_rudp_reliability_send_reliable_via(net_rudp_peer_t *peer,
                                          void *io_user,
                                          int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                          const net_udp_addr_t *to,
                                          uint64_t now_ms,
                                          uint16_t schema_id,
                                          const void *payload,
                                          size_t payload_size,
                                          uint16_t *out_sequence);

/**
 * @brief Resend any unacknowledged reliable packets past the resend interval using a sendto callback.
 */
int net_rudp_reliability_tick_resend_via(net_rudp_peer_t *peer,
                                        void *io_user,
                                        int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                        const net_udp_addr_t *to,
                                        uint64_t now_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_RUDP_RELIABILITY_SEND_H */
