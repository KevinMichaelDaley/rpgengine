#ifndef FERRUM_NET_RUDP_SENDTO_INTERNAL_H
#define FERRUM_NET_RUDP_SENDTO_INTERNAL_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/rudp/peer.h"

#ifdef __cplusplus
extern "C" {
#endif

int net_rudp_peer_send_unreliable_with_sendto(net_rudp_peer_t *peer,
                                              void *io_user,
                                              int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                              const net_udp_addr_t *to,
                                              uint64_t now_ms,
                                              uint16_t schema_id,
                                              const void *payload,
                                              size_t payload_size);

int net_rudp_peer_send_reliable_with_sendto(net_rudp_peer_t *peer,
                                            void *io_user,
                                            int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                            const net_udp_addr_t *to,
                                            uint64_t now_ms,
                                            uint16_t schema_id,
                                            const void *payload,
                                            size_t payload_size,
                                            uint16_t *out_sequence);

int net_rudp_peer_tick_resend_with_sendto(net_rudp_peer_t *peer,
                                          void *io_user,
                                          int (*sendto_cb)(void *io_user, const net_udp_addr_t *to, const void *data, size_t size),
                                          const net_udp_addr_t *to,
                                          uint64_t now_ms);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_RUDP_SENDTO_INTERNAL_H */
