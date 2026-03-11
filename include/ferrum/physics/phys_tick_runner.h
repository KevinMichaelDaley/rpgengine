/**
 * @file phys_tick_runner.h
 * @brief Continuous physics tick runner — runs on a dedicated OS thread.
 *
 * The runner spawns a dedicated pthread that loops continuously,
 * self-pacing at fixed_dt.  Each iteration:
 *   1. Drain spawn/mutation commands
 *   2. Run phys_world_tick_parallel()
 *   3. Drain corrections
 *   4. Signal tick completion (atomic counter)
 *   5. Sleep until next tick interval
 *
 * The main thread communicates via channels and reads bodies_curr
 * (always valid — the tick writes to bodies_next, then swaps).
 *
 * Lifecycle:
 *   1. phys_tick_runner_init()    — bind world, jobs, channels, callback
 *   2. phys_tick_runner_start()   — launch the dedicated thread
 *   3. phys_tick_runner_tick_id() — read latest completed tick (non-blocking)
 *   4. phys_tick_runner_stop()    — request stop + join thread
 *   5. phys_tick_runner_destroy() — cleanup
 *
 * Ownership: the runner borrows all pointers (world, jobs, channels).
 * The caller must keep them alive for the runner's lifetime.
 *
 * Thread safety: start/stop must be called from the main thread.
 * The tick loop runs on its own OS thread and dispatches parallel
 * physics jobs to the job system's worker pool.
 */

#ifndef FERRUM_PHYSICS_PHYS_TICK_RUNNER_H
#define FERRUM_PHYSICS_PHYS_TICK_RUNNER_H

#include <pthread.h>
#include <stdbool.h>
#include <stdatomic.h>
#include <stdint.h>

#include "ferrum/physics/phys_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations — avoids pulling full headers into every user. */
struct phys_world;
struct phys_job_context;
struct fr_topic_channel;
struct phys_game_state;

/**
 * @brief Post-tick callback signature.
 *
 * Invoked on the physics thread immediately after each tick completes
 * and corrections are drained.  Use for sending priority state updates
 * at physics tick rate.
 *
 * @param user   Opaque user pointer (set in init).
 * @param tick   Completed tick number (1-based, monotonically increasing).
 */
typedef void (*phys_tick_runner_post_tick_fn)(void *user, uint64_t tick);

/**
 * @brief Pre-tick callback signature.
 *
 * Invoked on the physics thread after commands are drained but before
 * phys_world_tick_parallel().  Use this to advance animation and push
 * kinematic body positions into the world.
 *
 * @param user   Opaque user pointer.
 * @param world  Physics world (bodies may be read/written).
 * @param tick   Tick number about to execute.
 */
typedef void (*phys_anim_pre_tick_fn)(void *user,
                                      struct phys_world *world,
                                      uint64_t tick);

/**
 * @brief Continuous physics tick runner.
 *
 * Stack-allocatable.  Must be initialized before use and destroyed
 * after stopping.
 */
typedef struct phys_tick_runner {
    struct phys_world        *world;      /**< Borrowed physics world. */
    struct phys_job_context  *jobs;       /**< Borrowed job context. */
    struct fr_topic_channel  *cmd_channel;/**< Spawns + mutations (drained before tick). */
    struct fr_topic_channel  *correction_channel; /**< SET_STATE corrections (drained after tick, may be NULL). */

    /** Optional callback invoked for each SPAWN_BODY during drain. */
    phys_cmd_spawn_callback_t spawn_cb;
    void                     *spawn_cb_user;

    /** Optional callback invoked before each tick (after command drain).
     *  Use this to advance animation and update kinematic body positions. */
    phys_anim_pre_tick_fn pre_tick_cb;
    void                 *pre_tick_cb_user;

    /** Optional callback invoked after each tick completes. */
    phys_tick_runner_post_tick_fn post_tick_cb;
    void                         *post_tick_cb_user;

    /** Optional game state for tier classification (borrowed, may be NULL).
     *  When NULL, all bodies default to T0.  Updated externally before
     *  each tick — the benign data race on positions is acceptable. */
    struct phys_game_state   *game_state;

    /** Monotonically increasing tick counter — incremented by the
     *  thread after each tick completes.  Main thread reads this to
     *  detect new ticks. */
    atomic_uint_fast64_t completed_ticks;

    /** Stop flag — main thread sets to 1, thread exits on next iteration. */
    atomic_int stop_requested;

    /** Pause flag — when 1, thread still runs but skips physics ticks.
     *  Commands are still drained so spawns/moves work while paused. */
    atomic_int paused;

    /** Single-step counter — when paused, if > 0 the thread runs one
     *  tick and decrements.  Set by phys_tick_runner_step_once(). */
    atomic_int step_requested;

    /** Set to 1 by the thread when it has exited its loop. */
    atomic_int stopped;

    /** Duration of the most recent physics tick in nanoseconds.
     *  Written by the tick thread, read by the main thread. */
    atomic_uint_fast64_t last_tick_duration_ns;

    /** CLOCK_MONOTONIC timestamp (ms) when the most recent physics tick
     *  completed.  Written by the tick thread, read by the main thread.
     *  Used to stamp body-state messages with the physics time. */
    atomic_uint_fast64_t last_tick_completion_ms;

    /** Dedicated OS thread handle. */
    pthread_t thread;

    /** True after start() has been called. */
    uint8_t running;

    /** When true, skip realtime pacing (benchmark mode). */
    uint8_t no_pacing;

    /** Pre-allocated mutation staging buffer.  Populated by
     *  phys_cmd_drain_spawns() before each tick, consumed by
     *  tick_parallel via world->pending_mutations. */
    uint8_t *mutation_buf;
    size_t   mutation_buf_cap;
} phys_tick_runner_t;

/**
 * @brief Initialize a tick runner.
 *
 * @param r              Runner to initialize.  Must not be NULL.
 * @param world          Physics world to tick.  Must not be NULL.
 * @param jobs           Job context for parallel dispatch.  Must not be NULL.
 * @param cmd_channel    Command channel for spawns/mutations (drained before tick, may be NULL).
 * @param correction_channel  Command channel for SET_STATE corrections (drained after tick, may be NULL).
 * @param spawn_cb       Callback for SPAWN_BODY results (may be NULL).
 * @param spawn_cb_user  Opaque pointer forwarded to spawn_cb.
 */
void phys_tick_runner_init(phys_tick_runner_t *r,
                           struct phys_world *world,
                           struct phys_job_context *jobs,
                           struct fr_topic_channel *cmd_channel,
                           struct fr_topic_channel *correction_channel,
                           phys_cmd_spawn_callback_t spawn_cb,
                           void *spawn_cb_user);

/**
 * @brief Tear down the runner.  No-op if NULL.
 *
 * Must only be called after stop() has completed.
 */
void phys_tick_runner_destroy(phys_tick_runner_t *r);

/**
 * @brief Launch the dedicated physics thread.
 *
 * Spawns an OS thread that loops forever (until stop is requested),
 * self-pacing at fixed_dt and dispatching parallel physics jobs to
 * the job system's worker pool.
 *
 * @param r  Runner.  Must not be NULL.  Must be initialized.
 */
void phys_tick_runner_start(phys_tick_runner_t *r);

/**
 * @brief Request stop and spin until the fiber exits.
 *
 * @param r  Runner (NULL-safe, no-op).
 */
void phys_tick_runner_stop(phys_tick_runner_t *r);

/**
 * @brief Read the latest completed tick number (non-blocking).
 *
 * @param r  Runner (NULL-safe, returns 0).
 * @return Number of completed ticks since start().
 */
uint64_t phys_tick_runner_tick_id(const phys_tick_runner_t *r);

/**
 * @brief Read the duration of the most recent physics tick (non-blocking).
 *
 * @param r  Runner (NULL-safe, returns 0).
 * @return Duration of the last completed tick in nanoseconds.
 */
uint64_t phys_tick_runner_last_tick_ns(const phys_tick_runner_t *r);

/**
 * @brief Read the CLOCK_MONOTONIC timestamp (ms) when the most recent
 *        physics tick completed (non-blocking).
 *
 * @param r  Runner (NULL-safe, returns 0).
 * @return Wall-clock completion time of the last tick in milliseconds.
 */
uint64_t phys_tick_runner_last_tick_completion_ms(const phys_tick_runner_t *r);

/* ── Pause / Resume / Step API ──────────────────────────────────────── */

/**
 * @brief Pause the physics simulation.
 *
 * The tick thread continues running but skips physics ticks.
 * Commands (spawns, moves) are still drained while paused.
 * Idempotent — calling on an already-paused runner is a no-op.
 *
 * @param r  Runner (NULL-safe, no-op).
 */
void phys_tick_runner_pause(phys_tick_runner_t *r);

/**
 * @brief Resume the physics simulation.
 *
 * Idempotent — calling on an already-running runner is a no-op.
 *
 * @param r  Runner (NULL-safe, no-op).
 */
void phys_tick_runner_resume(phys_tick_runner_t *r);

/**
 * @brief Advance exactly one physics tick while paused.
 *
 * Has no effect if the runner is not paused.  The tick thread will
 * execute one tick and then return to the paused state.
 *
 * @param r  Runner (NULL-safe, no-op).
 */
void phys_tick_runner_step_once(phys_tick_runner_t *r);

/**
 * @brief Query whether the runner is currently paused.
 *
 * @param r  Runner (NULL-safe, returns false).
 * @return true if paused, false if running or NULL.
 */
bool phys_tick_runner_is_paused(const phys_tick_runner_t *r);

/* ── Backward compatibility (kick/done/consume API) ─────────────── */
/* These thin wrappers exist so existing callers don't break.
 * kick() starts the runner if not already running.
 * done() always returns 1 (ticks complete continuously).
 * consume() is a no-op. */

/** Start the runner if not already running.  Backward compat. */
void phys_tick_runner_kick(phys_tick_runner_t *r);

/** Always returns 1.  Backward compat. */
int phys_tick_runner_done(const phys_tick_runner_t *r);

/** No-op.  Backward compat. */
void phys_tick_runner_consume(phys_tick_runner_t *r);

/** Stop the runner and wait.  Backward compat. */
void phys_tick_runner_wait(phys_tick_runner_t *r);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_TICK_RUNNER_H */
