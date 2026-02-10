/**
 * @file runtime_tx_pump.c
 * @brief TX pump loop and enqueue API.
 *
 * Non-static functions: 3 (enqueue, pump_once, pump_main).
 * pump_main is exported for the context file to reference but is not
 * part of the public API (thread entry point).
 */

#include <string.h>
#include <time.h>
#include <stdatomic.h>

#include "ferrum/net/client/runtime_tx.h"
#include "tx_internal.h"

/* ── Rate-limited sendto wrapper ───────────────────────────────── */

/**
 * State threaded through the rate-limited flush callback.
 */
typedef struct pump_ctx {
    fr_client_tx_sendto_fn sendto;
    void *user;
    uint32_t sent;
    uint32_t limit;   /* 0 = unlimited */
} pump_ctx_t;

/**
 * Callback passed to fr_rudp_stream_flush_send.
 * Forwards to the real sendto, but stops after limit.
 */
static int rate_limited_sendto(void *user, const uint8_t *data, size_t len) {
    pump_ctx_t *ctx = (pump_ctx_t *)user;
    if (ctx->limit > 0 && ctx->sent >= ctx->limit) {
        return -1;  /* signal flush to stop */
    }
    int rc = ctx->sendto(ctx->user, data, len);
    if (rc == 0) { ctx->sent++; }
    return rc;
}

/* ── Public API ────────────────────────────────────────────────── */

bool fr_client_tx_enqueue(fr_client_tx_t *tx, uint32_t channel_id,
                           const uint8_t *payload, size_t len) {
    if (!tx || !tx->stream) { return false; }
    if (channel_id >= tx->max_channels) { return false; }
    return fr_rudp_stream_send(tx->stream, channel_id, payload, len);
}

uint32_t fr_client_tx_pump_once(fr_client_tx_t *tx) {
    if (!tx || !tx->stream || !tx->sendto) { return 0; }

    pump_ctx_t ctx = {
        .sendto = tx->sendto,
        .user = tx->sendto_user,
        .sent = 0,
        .limit = tx->max_packets_per_pump,
    };

    fr_rudp_stream_flush_send(tx->stream, rate_limited_sendto, &ctx);
    return ctx.sent;
}

/* ── Thread entry point ────────────────────────────────────────── */

void *fr_client_tx_pump_main(void *arg) {
    fr_client_tx_t *tx = (fr_client_tx_t *)arg;
    while (atomic_load(&tx->running)) {
        uint32_t flushed = fr_client_tx_pump_once(tx);
        if (flushed == 0) {
            /* Nothing to send — sleep 1ms to avoid busy loop. */
            struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 };
            nanosleep(&ts, NULL);
        }
    }
    return NULL;
}
