/**
 * @file tick_loop.h
 * @brief Server tick loop: fixed-timestep accumulator with catch-up cap.
 *
 * Drives the per-tick pipeline:
 *   drain inbound → physics → encode replication → flush outbound
 *
 * Stack-allocatable. Caller invokes fr_server_tick_loop_step() each
 * iteration with the elapsed microseconds; the loop runs 0..N ticks
 * based on the accumulator and catch-up cap.
 *
 * Types: fr_server_tick_loop_config_t, fr_server_tick_loop_t.
 */
#ifndef FERRUM_SERVER_TICK_LOOP_H
#define FERRUM_SERVER_TICK_LOOP_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Per-stage callback signature.
 *
 * @param user  Opaque pointer from config.
 */
typedef void (*fr_server_tick_fn)(void *user);

/**
 * @brief Configuration for the server tick loop.
 */
typedef struct fr_server_tick_loop_config {
    /** Tick rate in Hz (e.g. 60). Must be > 0. */
    uint32_t tick_hz;

    /** Max ticks to run per step call (catch-up cap). 0 defaults to 1. */
    uint32_t max_catchup_ticks;

    /** Drain inbound messages/events. Called first each tick. Optional. */
    fr_server_tick_fn on_drain;

    /** Kick physics simulation. Called after drain. Optional. */
    fr_server_tick_fn on_physics;

    /** Encode replication (events + state) into outbound topics. Optional. */
    fr_server_tick_fn on_encode;

    /** Flush outbound topics to network I/O. Called last each tick. Optional. */
    fr_server_tick_fn on_flush;

    /** User pointer passed to all callbacks. */
    void *user;
} fr_server_tick_loop_config_t;

/**
 * @brief Server tick loop state. Stack-allocatable.
 */
typedef struct fr_server_tick_loop {
    /** Tick period in microseconds. */
    uint64_t tick_period_us;

    /** Accumulated time not yet consumed (microseconds). */
    uint64_t accumulator_us;

    /** Current tick counter. */
    uint64_t tick_id;

    /** Max ticks per step. */
    uint32_t max_catchup;

    /** Per-stage callbacks. */
    fr_server_tick_fn on_drain;
    fr_server_tick_fn on_physics;
    fr_server_tick_fn on_encode;
    fr_server_tick_fn on_flush;

    /** User pointer for callbacks. */
    void *user;
} fr_server_tick_loop_t;

/* ── API ───────────────────────────────────────────────────────── */

/**
 * @brief Initialize a tick loop.
 *
 * @param loop  Loop to initialize (non-NULL).
 * @param cfg   Configuration (non-NULL, tick_hz > 0).
 * @return 0 on success, -1 on invalid args.
 */
int fr_server_tick_loop_init(fr_server_tick_loop_t *loop,
                             const fr_server_tick_loop_config_t *cfg);

/**
 * @brief Advance the tick loop by elapsed_us microseconds.
 *
 * Runs 0..max_catchup_ticks based on the accumulator.
 * Each tick invokes: on_drain → on_physics → on_encode → on_flush.
 *
 * @param loop        Initialized loop (non-NULL).
 * @param elapsed_us  Microseconds elapsed since last step.
 * @return Number of ticks executed this step (0..max_catchup).
 */
int fr_server_tick_loop_step(fr_server_tick_loop_t *loop,
                             uint64_t elapsed_us);

/**
 * @brief Get the current tick counter.
 *
 * @param loop  Initialized loop (non-NULL).
 * @return Current tick ID.
 */
uint64_t fr_server_tick_loop_tick_id(const fr_server_tick_loop_t *loop);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SERVER_TICK_LOOP_H */
