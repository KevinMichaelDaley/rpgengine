// Client RX runtime context: create/destroy/start/stop
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_rx.h"
#include "internal.h"

static void channel_init(fr_channel_state *ch) {
    atomic_init(&ch->seq_next, 1u);
    atomic_init(&ch->pending, 0u);
    ch->head = ch->tail = NULL;
    for (int i = 0; i < 8; ++i) { ch->ooo_msgs[i] = NULL; ch->ooo_seq[i] = 0; }
}

fr_client_rx_t *fr_client_rx_create(const fr_client_rx_config_t *cfg) {
    fr_client_rx_t *rx = (fr_client_rx_t *)malloc(sizeof(fr_client_rx_t));
    if (!rx) return NULL;
    memset(rx, 0, sizeof(*rx));
    rx->max_channels = (cfg && cfg->max_channels) ? cfg->max_channels : 16u;
    rx->max_pending = (cfg && cfg->max_pending_per_channel) ? cfg->max_pending_per_channel : 64u;
    rx->recv_cb = cfg ? cfg->recv_cb : NULL;
    rx->recv_user = cfg ? cfg->recv_user : NULL;
    rx->channels = (fr_channel_state *)calloc(rx->max_channels, sizeof(fr_channel_state));
    if (!rx->channels) { free(rx); return NULL; }
    for (uint32_t i = 0; i < rx->max_channels; ++i) channel_init(&rx->channels[i]);
    atomic_init(&rx->running, false);
    return rx;
}

static void channel_clear(fr_channel_state *ch) {
    struct fr_msg_node *n = ch->head;
    while (n) {
        struct fr_msg_node *next = n->next;
        free(n->data);
        free(n);
        n = next;
    }
    ch->head = ch->tail = NULL;
    atomic_store(&ch->pending, 0u);
}

void fr_client_rx_destroy(fr_client_rx_t *rx) {
    if (!rx) return;
    // Ensure stopped
    (void)fr_client_rx_stop(rx);
    if (rx->channels) {
        for (uint32_t i = 0; i < rx->max_channels; ++i) channel_clear(&rx->channels[i]);
        free(rx->channels);
    }
    free(rx);
}

static void *rx_thread_main(void *arg) {
    fr_client_rx_t *rx = (fr_client_rx_t *)arg;
    uint8_t buf[1500];
    while (atomic_load(&rx->running)) {
        if (!rx->recv_cb) {
            // No socket path implemented yet; sleep briefly
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
            nanosleep(&ts, NULL);
            continue;
        }
        ssize_t n = rx->recv_cb(rx->recv_user, buf, sizeof(buf));
        if (n > 0) {
            // Reuse inject path for parsing
            (void)fr_client_rx_inject(rx, buf, (size_t)n);
        }
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
