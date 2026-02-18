// Client RX runtime context: create/destroy/start/stop
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

// Using fr_rudp_stream_t for reassembly; no per-channel init required.

fr_client_rx_t *fr_client_rx_create(const fr_client_rx_config_t *cfg) {
    fr_client_rx_t *rx = (fr_client_rx_t *)malloc(sizeof(fr_client_rx_t));
    if (!rx) return NULL;
    memset(rx, 0, sizeof(*rx));
    rx->max_channels = (cfg && cfg->max_channels) ? cfg->max_channels : 16u;
    rx->max_pending = (cfg && cfg->max_pending_per_channel) ? cfg->max_pending_per_channel : 64u;
    rx->recv_cb = cfg ? cfg->recv_cb : NULL;
    rx->recv_user = cfg ? cfg->recv_user : NULL;
    rx->topics = cfg ? cfg->topics : NULL;
    rx->num_topics = cfg ? cfg->num_topics : 0u;
    rx->sock_initialized = 0;
    if (cfg && cfg->socket) {
        rx->sock = *cfg->socket;
        rx->sock_initialized = 1;
    } else {
        rx->sock.fd = -1;
        rx->sock.initialized = 0;
    }
    // Create reliable UDP stream with topics configured for pumping
    fr_rudp_stream_config_t scfg = {0};
    scfg.reliable_channels = rx->max_channels;
    scfg.reliable_slot_count = rx->max_pending;
    scfg.max_payload_size = 1500u;
    scfg.topics = rx->topics;
    scfg.num_topics = rx->num_topics;
    rx->stream = fr_rudp_stream_create(&scfg);
    if (!rx->stream) { free(rx); return NULL; }
    rx->frame_buf = (uint8_t *)malloc(RX_FRAME_BUF_SIZE);
    if (!rx->frame_buf) { fr_rudp_stream_destroy(rx->stream); free(rx); return NULL; }
    atomic_init(&rx->running, false);
    return rx;
}

// Legacy channel_clear removed; stream owns buffers.

void fr_client_rx_destroy(fr_client_rx_t *rx) {
    if (!rx) return;
    // Ensure stopped
    (void)fr_client_rx_stop(rx);
    if (rx->stream) {
        fr_rudp_stream_destroy(rx->stream);
        rx->stream = NULL;
    }
    free(rx->frame_buf);
    rx->frame_buf = NULL;
    if (rx->sock_initialized) {
        net_udp_socket_close(&rx->sock);
        rx->sock_initialized = 0;
    }
    free(rx);
}

static void *rx_thread_main(void *arg) {
    fr_client_rx_t *rx = (fr_client_rx_t *)arg;
    uint8_t buf[1500];
    while (atomic_load(&rx->running)) {
        if (rx->recv_cb) {
            ssize_t n = rx->recv_cb(rx->recv_user, buf, sizeof(buf));
            if (n > 0) (void)fr_client_rx_inject(rx, buf, (size_t)n);
            continue;
        }
        if (rx->sock_initialized) {
            size_t out_size = 0;
            net_udp_addr_t from;
            int r = net_udp_socket_recvfrom(&rx->sock, &from, buf, sizeof(buf), &out_size);
            if (r == NET_UDP_SOCKET_OK && out_size > 0) {
                (void)fr_client_rx_inject(rx, buf, out_size);
            } else if (r == NET_UDP_SOCKET_TIMEOUT || r == NET_UDP_SOCKET_EMPTY) {
                // brief sleep to avoid busy loop in timeout/empty
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
                nanosleep(&ts, NULL);
            } else {
                // System error or invalid; sleep and retry
                struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
                nanosleep(&ts, NULL);
            }
            continue;
        }
        // Neither callback nor socket; sleep
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 2000000 };
        nanosleep(&ts, NULL);
    }
    return NULL;
}

bool fr_client_rx_start(fr_client_rx_t *rx) {
    if (!rx) return false;
    if (atomic_exchange(&rx->running, true)) return true;
    return pthread_create(&rx->thread, NULL, rx_thread_main, rx) == 0;
}

bool fr_client_rx_stop(fr_client_rx_t *rx) {
    if (!rx) return false;
    if (!atomic_exchange(&rx->running, false)) return true;
    return pthread_join(rx->thread, NULL) == 0;
}

bool fr_client_rx_bind_ipv4(fr_client_rx_t *rx, uint8_t a, uint8_t b, uint8_t c, uint8_t d, uint16_t port) {
    if (!rx) return false;
    if (rx->sock_initialized) return true;
    if (net_udp_socket_open(&rx->sock) != NET_UDP_SOCKET_OK) return false;
    net_udp_addr_t addr;
    if (net_udp_addr_ipv4(&addr, a, b, c, d, port) != NET_UDP_SOCKET_OK) {
        net_udp_socket_close(&rx->sock);
        return false;
    }
    if (net_udp_socket_bind(&rx->sock, &addr) != NET_UDP_SOCKET_OK) {
        net_udp_socket_close(&rx->sock);
        return false;
    }
    (void)net_udp_socket_set_recv_timeout_ms(&rx->sock, 10);
    rx->sock_initialized = 1;
    return true;
}
