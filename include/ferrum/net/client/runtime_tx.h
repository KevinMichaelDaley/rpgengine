/**
 * @file runtime_tx.h
 * @brief Client network TX runtime: outbound queue + pump thread.
 *
 * Game jobs enqueue messages via fr_client_tx_enqueue(). A dedicated pump
 * thread (or manual fr_client_tx_pump_once) drains the outbound stream
 * through a sendto callback.
 *
 * Ownership: caller owns context returned by create and must destroy.
 * Threading: start/stop manage an internal TX thread; enqueue is thread-safe.
 *
 * Types: fr_client_tx_config_t, fr_client_tx_t (opaque).
 */
#ifndef FERRUM_NET_CLIENT_RUNTIME_TX_H
#define FERRUM_NET_CLIENT_RUNTIME_TX_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for transmitting a serialized frame.
 *
 * @param user  Opaque user pointer from config.
 * @param data  Frame bytes to send.
 * @param len   Frame length in bytes.
 * @return 0 on success, non-zero to signal send failure (pump will stop).
 */
typedef int (*fr_client_tx_sendto_fn)(void *user, const uint8_t *data,
                                      size_t len);

/**
 * @brief Configuration for client TX runtime.
 *
 * All fields optional except sendto callback.
 */
typedef struct fr_client_tx_config {
    /** Max outbound channels; defaults to 4 if 0. */
    uint32_t max_channels;

    /** Max pending messages per channel in outbound stream; defaults to 64. */
    uint32_t max_pending_per_channel;

    /** Max payload size per message; defaults to 1024 if 0. */
    uint32_t max_payload_size;

    /**
     * Rate limit: max packets flushed per pump cycle. 0 = unlimited.
     * Remaining messages stay queued for the next pump cycle.
     */
    uint32_t max_packets_per_pump;

    /** Transmit callback (required). */
    fr_client_tx_sendto_fn sendto;

    /** User pointer passed to sendto callback. */
    void *sendto_user;
} fr_client_tx_config_t;

/** Opaque client TX runtime context. */
typedef struct fr_client_tx fr_client_tx_t;

/* ── Lifecycle ─────────────────────────────────────────────────── */

/**
 * @brief Create a TX runtime context.
 *
 * Allocates internal outbound stream. Returns NULL on failure.
 *
 * @param cfg  Configuration (non-NULL, sendto required).
 * @return New TX context, or NULL.
 */
fr_client_tx_t *fr_client_tx_create(const fr_client_tx_config_t *cfg);

/**
 * @brief Destroy a TX runtime context.
 *
 * Stops the thread if running, frees all resources.
 * Safe to call on NULL.
 */
void fr_client_tx_destroy(fr_client_tx_t *tx);

/* ── Thread management ─────────────────────────────────────────── */

/**
 * @brief Start the TX pump thread.
 *
 * @return true on success, false if already running or thread create fails.
 */
bool fr_client_tx_start(fr_client_tx_t *tx);

/**
 * @brief Stop and join the TX pump thread.
 *
 * @return true on success, false if not running or join fails.
 */
bool fr_client_tx_stop(fr_client_tx_t *tx);

/* ── Enqueue (thread-safe) ─────────────────────────────────────── */

/**
 * @brief Enqueue a message for outbound transmission on a channel.
 *
 * Thread-safe: may be called from game jobs while the pump thread runs.
 *
 * @param tx          TX context (non-NULL).
 * @param channel_id  Target channel (must be < max_channels).
 * @param payload     Message payload (non-NULL if len > 0).
 * @param len         Payload length in bytes.
 * @return true if enqueued, false on invalid channel or queue full.
 */
bool fr_client_tx_enqueue(fr_client_tx_t *tx, uint32_t channel_id,
                           const uint8_t *payload, size_t len);

/* ── Manual pump (for testing) ─────────────────────────────────── */

/**
 * @brief Run one pump cycle: drain outbound queue → sendto.
 *
 * Respects max_packets_per_pump rate limit.
 * Can be called without starting the thread (for testing).
 *
 * @param tx  TX context (non-NULL).
 * @return Number of frames flushed.
 */
uint32_t fr_client_tx_pump_once(fr_client_tx_t *tx);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NET_CLIENT_RUNTIME_TX_H */
