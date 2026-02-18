#ifndef FERRUM_NET_CLIENT_INTERNAL_H
#define FERRUM_NET_CLIENT_INTERNAL_H

#include <stddef.h>
#include <stdint.h>
#include <pthread.h>
#include <stdatomic.h>
#include "ferrum/net/udp_socket.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/net/stream.h"

/** Maximum frame size for rx_inject: 4-byte header + max payload. */
#define RX_FRAME_BUF_SIZE 2048u

typedef struct fr_client_rx_t {
    uint32_t max_channels;
    uint32_t max_pending;
    fr_rudp_stream_t *stream;
    pthread_t thread;
    atomic_bool running;
    ssize_t (*recv_cb)(void *user, uint8_t *buf, size_t cap);
    void *recv_user;
    net_udp_socket_t sock;
    uint8_t sock_initialized;
    fr_topic_channel_t **topics;
    uint32_t num_topics;
    /** Pre-allocated frame buffer for rx_inject (avoids per-packet malloc). */
    uint8_t *frame_buf;
} fr_client_rx_t;

#endif // FERRUM_NET_CLIENT_INTERNAL_H
