/**
 * @file runtime_tx_context.c
 * @brief TX runtime lifecycle: create, destroy, start, stop.
 *
 * Non-static functions: 4 (create, destroy, start, stop).
 */

#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_tx.h"
#include "tx_internal.h"

/* Forward declaration: pump loop entry point (in runtime_tx_pump.c). */
extern void *fr_client_tx_pump_main(void *arg);

fr_client_tx_t *fr_client_tx_create(const fr_client_tx_config_t *cfg) {
    if (!cfg || !cfg->sendto) { return NULL; }

    fr_client_tx_t *tx = (fr_client_tx_t *)malloc(sizeof(fr_client_tx_t));
    if (!tx) { return NULL; }
    memset(tx, 0, sizeof(*tx));

    tx->max_channels = (cfg->max_channels > 0) ? cfg->max_channels : 4u;
    tx->max_packets_per_pump = cfg->max_packets_per_pump;
    tx->sendto = cfg->sendto;
    tx->sendto_user = cfg->sendto_user;

    /* Create outbound stream with the requested channel/slot config. */
    uint32_t pending = (cfg->max_pending_per_channel > 0)
                           ? cfg->max_pending_per_channel
                           : 64u;
    uint32_t payload_sz = (cfg->max_payload_size > 0)
                              ? cfg->max_payload_size
                              : 1024u;

    fr_rudp_stream_config_t scfg = {0};
    scfg.reliable_channels = tx->max_channels;
    scfg.reliable_slot_count = pending;
    scfg.max_payload_size = payload_sz;
    /* No topic channels on outbound side. */
    scfg.topics = NULL;
    scfg.num_topics = 0;

    tx->stream = fr_rudp_stream_create(&scfg);
    if (!tx->stream) {
        free(tx);
        return NULL;
    }

    atomic_init(&tx->running, false);
    return tx;
}

void fr_client_tx_destroy(fr_client_tx_t *tx) {
    if (!tx) { return; }
    fr_client_tx_stop(tx);
    if (tx->stream) {
        fr_rudp_stream_destroy(tx->stream);
        tx->stream = NULL;
    }
    free(tx);
}

bool fr_client_tx_start(fr_client_tx_t *tx) {
    if (!tx) { return false; }
    if (atomic_exchange(&tx->running, true)) { return true; } /* already running */
    if (pthread_create(&tx->thread, NULL, fr_client_tx_pump_main, tx) != 0) {
        atomic_store(&tx->running, false);
        return false;
    }
    return true;
}

bool fr_client_tx_stop(fr_client_tx_t *tx) {
    if (!tx) { return false; }
    if (!atomic_exchange(&tx->running, false)) { return true; } /* not running */
    return pthread_join(tx->thread, NULL) == 0;
}
