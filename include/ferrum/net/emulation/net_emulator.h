#ifndef FERRUM_NET_EMULATION_NET_EMULATOR_H
#define FERRUM_NET_EMULATION_NET_EMULATOR_H

/**
 * @file net_emulator.h
 * @brief In-process network condition emulator (delay, jitter, loss, reorder).
 *
 * Sits between the application and the UDP socket layer, queuing packets
 * with configurable latency, jitter distribution, packet loss, and
 * reorder probability.  Designed as a core engine feature — demos and
 * tests invoke it via API.
 *
 * Ownership: Caller allocates and owns the net_emulator_t.  The emulator
 *            owns its internal packet queue (heap-allocated).
 * Nullability: All public APIs validate pointers.
 * Threading: NOT thread-safe — caller must synchronize.
 */

#include <stddef.h>
#include <stdint.h>

#include "ferrum/net/udp_socket.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ── Error codes ──────────────────────────────────────────────── */

#define NET_EMU_OK          0
#define NET_EMU_ERR_INVALID -1
#define NET_EMU_ERR_FULL    -2
#define NET_EMU_ERR_ALLOC   -3

/* ── Jitter distribution ──────────────────────────────────────── */

/**
 * @brief Distribution used to sample per-packet jitter.
 *
 * UNIFORM: delay ± jitter_ms (uniform random in range).
 * NORMAL:  Gaussian with mean=delay_ms, stddev=jitter_ms.
 *          Clamped to [0, delay_ms + 4*jitter_ms].
 * LOG_NORMAL: log-normal distribution — models real-world jitter
 *             better (right-skewed, occasional large spikes).
 *             mu and sigma derived from delay_ms and jitter_ms.
 */
typedef enum net_emu_distribution {
    NET_EMU_DIST_UNIFORM    = 0,
    NET_EMU_DIST_NORMAL     = 1,
    NET_EMU_DIST_LOG_NORMAL = 2,
} net_emu_distribution_t;

/* ── Configuration ────────────────────────────────────────────── */

/**
 * @brief Network emulation parameters.
 *
 * All fields can be changed at runtime via net_emulator_configure().
 * Setting delay_ms=0, jitter_ms=0, loss_pct=0, reorder_pct=0
 * effectively makes the emulator a pass-through (but packets still
 * transit the queue — use net_emulator_set_enabled() to bypass entirely).
 */
typedef struct net_emu_config {
    float    delay_ms;       /**< Base one-way delay in milliseconds. */
    float    jitter_ms;      /**< Jitter magnitude (interpretation depends on distribution). */
    float    loss_pct;       /**< Packet loss probability [0..100]. */
    float    reorder_pct;    /**< Probability a packet is reordered [0..100]. */
    float    duplicate_pct;  /**< Probability a packet is duplicated [0..100]. */
    net_emu_distribution_t distribution; /**< Jitter distribution type. */
} net_emu_config_t;

/* ── Queued packet (opaque internal, exposed for sizing) ──────── */

/** Maximum payload size for a queued packet. */
#define NET_EMU_MAX_PACKET_SIZE 1500u

/** Maximum number of packets in the delay queue. */
#define NET_EMU_QUEUE_CAPACITY  512u

/**
 * @brief A single queued packet awaiting release.
 */
typedef struct net_emu_packet {
    uint8_t        data[NET_EMU_MAX_PACKET_SIZE];
    size_t         size;          /**< Actual payload size. */
    net_udp_addr_t addr;          /**< Destination (send) or source (recv). */
    uint64_t       release_us;    /**< Monotonic time (µs) at which to release. */
    uint8_t        occupied;      /**< 1 if slot is in use, 0 if free. */
} net_emu_packet_t;

/* ── Emulator instance ────────────────────────────────────────── */

/**
 * @brief Network condition emulator.
 *
 * Maintains a delay queue of packets and releases them according
 * to the configured latency/jitter/loss/reorder parameters.
 */
typedef struct net_emulator {
    net_emu_config_t  config;      /**< Current emulation parameters. */
    net_emu_packet_t *queue;       /**< Delay queue (heap-allocated). */
    uint32_t          queue_cap;   /**< Queue capacity. */
    uint32_t          queue_count; /**< Number of occupied slots. */
    uint32_t          rng_state;   /**< PRNG state (xorshift32). */
    uint8_t           enabled;     /**< 1 = emulation active, 0 = bypass. */
} net_emulator_t;

/* ── Lifecycle ────────────────────────────────────────────────── */

/**
 * @brief Return a zero/pass-through default config.
 */
net_emu_config_t net_emu_config_default(void);

/**
 * @brief Initialize the emulator with given config.
 *
 * Allocates the internal packet queue.  The emulator starts enabled.
 *
 * @param emu     Emulator to initialize.  Must not be NULL.
 * @param config  Initial configuration.  Must not be NULL.
 * @param seed    PRNG seed (0 uses a default based on PID + time).
 * @return NET_EMU_OK on success, NET_EMU_ERR_* on failure.
 */
int net_emulator_init(net_emulator_t *emu,
                      const net_emu_config_t *config,
                      uint32_t seed);

/**
 * @brief Destroy the emulator, freeing the packet queue.
 *
 * NULL-safe, idempotent.
 */
void net_emulator_destroy(net_emulator_t *emu);

/* ── Runtime control ──────────────────────────────────────────── */

/**
 * @brief Update emulation parameters at runtime.
 *
 * Takes effect immediately for newly queued packets.
 * Already-queued packets keep their scheduled release time.
 */
int net_emulator_configure(net_emulator_t *emu,
                           const net_emu_config_t *config);

/**
 * @brief Enable or disable the emulator.
 *
 * When disabled, submit/flush are no-ops and the caller should
 * send/recv directly.  Disabling does NOT drain the queue — call
 * net_emulator_flush() first if you want pending packets delivered.
 */
void net_emulator_set_enabled(net_emulator_t *emu, int enabled);

/**
 * @brief Check if the emulator is enabled.
 */
int net_emulator_is_enabled(const net_emulator_t *emu);

/* ── Packet submission ────────────────────────────────────────── */

/**
 * @brief Submit a packet into the delay queue.
 *
 * The packet is copied into the queue and scheduled for release
 * based on the current config (delay + jitter + loss + reorder).
 * If the packet is "lost" (random drop), it is silently discarded
 * and NET_EMU_OK is still returned.
 *
 * @param emu   Emulator instance.
 * @param addr  Destination/source address to associate with packet.
 * @param data  Packet payload.
 * @param size  Payload size in bytes (must be <= NET_EMU_MAX_PACKET_SIZE).
 * @param now_us Current monotonic time in microseconds.
 * @return NET_EMU_OK, NET_EMU_ERR_INVALID, or NET_EMU_ERR_FULL.
 */
int net_emulator_submit(net_emulator_t *emu,
                        const net_udp_addr_t *addr,
                        const void *data,
                        size_t size,
                        uint64_t now_us);

/* ── Packet retrieval ─────────────────────────────────────────── */

/**
 * @brief Pop the next packet whose release time has passed.
 *
 * Returns packets in release-time order (earliest first).
 * If no packet is ready, returns NET_EMU_ERR_INVALID (queue empty
 * or no packet due yet).
 *
 * @param emu       Emulator instance.
 * @param out_addr  Receives the packet's address.
 * @param out_data  Buffer to receive packet payload.
 * @param out_cap   Capacity of out_data buffer.
 * @param out_size  Receives actual payload size.
 * @param now_us    Current monotonic time in microseconds.
 * @return NET_EMU_OK if a packet was popped, NET_EMU_ERR_INVALID if none ready.
 */
int net_emulator_pop(net_emulator_t *emu,
                     net_udp_addr_t *out_addr,
                     void *out_data,
                     size_t out_cap,
                     size_t *out_size,
                     uint64_t now_us);

/**
 * @brief Return the number of packets currently queued.
 */
uint32_t net_emulator_pending(const net_emulator_t *emu);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_NET_EMULATION_NET_EMULATOR_H */
