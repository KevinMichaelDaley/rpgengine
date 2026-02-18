/**
 * @file time_sync.h
 * @brief Network time synchronization and jitter buffer.
 *
 * Time sync model:
 *   - Server embeds its timestamp in each outgoing packet.
 *   - Client records local arrival time and feeds both into
 *     net_time_sync_sample().
 *   - Offset = server_time - client_time, filtered by a sliding-window
 *     median to reject jitter outliers.
 *   - A drift clamp limits how fast the applied offset changes per
 *     update to avoid visual pops on the client.
 *
 * Jitter buffer:
 *   - Tracks the variance between expected and actual arrival times.
 *   - Produces a safety margin used to delay interpolation, ensuring
 *     packets are available before they're needed.
 *
 * Ownership: all storage is inline (no dynamic allocation).
 * NULL-safe: all public functions check for NULL inputs.
 */

#ifndef FERRUM_NET_TIME_SYNC_H
#define FERRUM_NET_TIME_SYNC_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ── Constants ─────────────────────────────────────────────────── */

/** Maximum sample window size for the median filter. */
#define NET_TIME_SYNC_MAX_WINDOW 32

/** Maximum sample window for jitter buffer. */
#define NET_JITTER_BUF_MAX_WINDOW 32

/* ── Time sync ─────────────────────────────────────────────────── */

/**
 * @brief Time synchronization state: offset estimator with drift clamp.
 *
 * Inline storage; no pointers to external buffers.
 */
typedef struct net_time_sync {
    int64_t samples[NET_TIME_SYNC_MAX_WINDOW]; /**< Ring of offset samples. */
    int64_t scratch[NET_TIME_SYNC_MAX_WINDOW]; /**< Scratch space for median computation. */
    uint32_t window_size;       /**< Configured window (≤ MAX_WINDOW). */
    uint32_t sample_count;      /**< Samples written so far. */
    uint32_t write_idx;         /**< Next write position in ring. */
    int64_t applied_offset;     /**< Drift-clamped output offset. */
    int64_t max_drift_per_update; /**< Max offset change per sample. */
    uint8_t initialized;        /**< Non-zero after first sample. */
} net_time_sync_t;

/**
 * @brief Jitter buffer: tracks arrival jitter and produces a margin.
 *
 * Inline storage; no pointers.
 */
typedef struct net_jitter_buffer {
    uint64_t jitter_samples[NET_JITTER_BUF_MAX_WINDOW]; /**< Abs jitter values. */
    uint32_t window_size;       /**< Configured window. */
    uint32_t sample_count;      /**< Samples written. */
    uint32_t write_idx;         /**< Next write position. */
} net_jitter_buffer_t;

/* ── Public API: time sync ─────────────────────────────────────── */

/**
 * @brief Initialize time sync state.
 *
 * @param sync               State to initialize (non-NULL).
 * @param window_size        Median filter window (clamped to MAX_WINDOW).
 * @param max_drift_per_update Max offset change per sample (ms).
 */
void net_time_sync_init(net_time_sync_t *sync,
                        uint32_t window_size,
                        int64_t max_drift_per_update);

/**
 * @brief Feed a time sample: server timestamp + client arrival time.
 *
 * Computes raw offset = server_time_ms - client_time_ms, stores in
 * the ring, updates the median, and applies drift clamping.
 *
 * @param sync            Time sync state (non-NULL).
 * @param server_time_ms  Server's timestamp from the packet.
 * @param client_time_ms  Client's local time when packet arrived.
 */
void net_time_sync_sample(net_time_sync_t *sync,
                          uint64_t server_time_ms,
                          uint64_t client_time_ms);

/**
 * @brief Get the current drift-clamped offset estimate.
 *
 * estimated_server_now = client_now + offset.
 *
 * @param sync  Time sync state (non-NULL).
 * @return Offset in milliseconds (may be negative), or 0 if NULL.
 */
int64_t net_time_sync_offset(const net_time_sync_t *sync);

/* ── Public API: jitter buffer ─────────────────────────────────── */

/**
 * @brief Initialize jitter buffer state.
 *
 * @param jbuf         Jitter buffer to initialize (non-NULL).
 * @param window_size  Sample window (clamped to MAX_WINDOW).
 */
void net_jitter_buffer_init(net_jitter_buffer_t *jbuf,
                            uint32_t window_size);

/**
 * @brief Feed a jitter sample: expected vs actual arrival time.
 *
 * @param jbuf         Jitter buffer (non-NULL).
 * @param expected_ms  Expected arrival time (e.g., last + interval).
 * @param actual_ms    Actual arrival time.
 */
void net_jitter_buffer_sample(net_jitter_buffer_t *jbuf,
                              uint64_t expected_ms,
                              uint64_t actual_ms);

/**
 * @brief Get the current jitter margin (interpolation delay).
 *
 * Returns the maximum observed jitter in the window, providing a
 * safety margin for interpolation timing.
 *
 * @param jbuf  Jitter buffer (non-NULL).
 * @return Margin in milliseconds, or 0 if NULL / no samples.
 */
uint64_t net_jitter_buffer_margin(const net_jitter_buffer_t *jbuf);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_NET_TIME_SYNC_H */
