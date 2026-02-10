/**
 * @file tx_internal.h
 * @brief Internal definition of fr_client_tx_t for TX runtime sources.
 */
#ifndef FERRUM_NET_CLIENT_TX_INTERNAL_H
#define FERRUM_NET_CLIENT_TX_INTERNAL_H

#include <pthread.h>
#include <stdatomic.h>
#include <stdint.h>

#include "ferrum/net/client/runtime_tx.h"
#include "ferrum/net/stream.h"

typedef struct fr_client_tx {
    /** Outbound reliable stream (owns send slots). */
    fr_rudp_stream_t *stream;

    /** Max channel count. */
    uint32_t max_channels;

    /** Rate limit: max packets per pump. 0 = unlimited. */
    uint32_t max_packets_per_pump;

    /** Sendto callback + user. */
    fr_client_tx_sendto_fn sendto;
    void *sendto_user;

    /** Pump thread state. */
    pthread_t thread;
    atomic_bool running;
} fr_client_tx_t;

#endif /* FERRUM_NET_CLIENT_TX_INTERNAL_H */
