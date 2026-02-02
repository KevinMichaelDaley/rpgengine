/**
 * Client network RX runtime: UDP receive and reliable stream reassembly.
 *
 * Public API defines an opaque context and a configuration struct.
 * Ownership: caller owns context returned by create and must destroy.
 * Threading: start/stop manage an internal RX thread; inject/pop are safe
 * to call from tests or other threads as documented below.
 */
#ifndef FERRUM_NET_CLIENT_RUNTIME_RX_H
#define FERRUM_NET_CLIENT_RUNTIME_RX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>
#include "ferrum/net/udp_socket.h"
#include "ferrum/net/topic_channel.h"

/** Configuration for client RX runtime. All fields optional. */
typedef struct fr_client_rx_config_t {
    /** Max channels supported; defaults to 16 if 0. */
    uint32_t max_channels;
    /** Max pending per-channel messages buffered; defaults to 64 if 0. */
    uint32_t max_pending_per_channel;
    /** Optional callback-based receive function for testing. If NULL, RX thread uses recvfrom. */
    ssize_t (*recv_cb)(void *user, uint8_t *buf, size_t cap);
    void *recv_user;
    /** Optional UDP socket to use; if NULL, fr_client_rx_bind_ipv4 must be called before start. */
    net_udp_socket_t *socket;
    /** Optional topic channels array; when provided, decoded messages are pushed to topics by channel_id. */
    fr_topic_channel_t **topics;
    uint32_t num_topics;
} fr_client_rx_config_t;

/** Opaque client RX runtime context. */
typedef struct fr_client_rx_t fr_client_rx_t;

/** Create a client RX runtime context. Returns NULL on allocation failure. */
fr_client_rx_t *fr_client_rx_create(const fr_client_rx_config_t *cfg);

/** Destroy a client RX runtime context. Safe to call if not started. */
void fr_client_rx_destroy(fr_client_rx_t *rx);

/** Start the RX thread. Returns false on thread or socket init error. */
bool fr_client_rx_start(fr_client_rx_t *rx);

/** Stop and join the RX thread. Returns false on join timeout/failure. */
bool fr_client_rx_stop(fr_client_rx_t *rx);

/**
 * Inject a raw frame directly into the reassembly path. Test-only helper.
 * Returns false if the frame is invalid or buffers are full.
 */
bool fr_client_rx_inject(fr_client_rx_t *rx, const uint8_t *data, size_t len);

/**
 * Pop the next in-order message for a channel.
 * On success, writes up to *inout_len bytes into out and updates *inout_len.
 * Returns false if no message available.
 */
bool fr_client_rx_pop_message(fr_client_rx_t *rx, uint32_t channel_id, uint8_t *out, size_t *inout_len);

/** Bind an internal UDP socket to an IPv4 address for receiving. */
bool fr_client_rx_bind_ipv4(fr_client_rx_t *rx, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port);

#endif // FERRUM_NET_CLIENT_RUNTIME_RX_H
