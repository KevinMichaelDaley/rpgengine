/**
 * @file phys_tick_runner.h
 * @brief Async physics tick runner: kick / poll / consume lifecycle.
 *
 * Wraps phys_world_tick_parallel() with an asynchronous dispatch
 * pattern so the caller (server main loop, client render loop, etc.)
 * never blocks on physics.  The runner drains a command channel at the
 * start of each tick and invokes a user-supplied spawn callback for
 * each SPAWN_BODY command.
 *
 * Lifecycle:
 *   1. phys_tick_runner_init()    — bind world, jobs, channel, callback
 *   2. phys_tick_runner_kick()    — dispatch one tick (non-blocking)
 *   3. phys_tick_runner_done()    — poll completion (non-blocking)
 *   4. phys_tick_runner_consume() — acknowledge completion, allow next kick
 *   5. phys_tick_runner_wait()    — blocking spin (shutdown only)
 *   6. phys_tick_runner_destroy() — cleanup
 *
 * Ownership: the runner borrows all pointers (world, jobs, channel).
 * The caller must keep them alive for the runner's lifetime.
 *
 * Thread safety: kick/done/consume/wait must be called from a single
 * thread (the main loop).  The tick itself runs as a fiber job.
 */

#ifndef FERRUM_PHYSICS_PHYS_TICK_RUNNER_H
#define FERRUM_PHYSICS_PHYS_TICK_RUNNER_H

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

/**
 * @brief Async physics tick runner.
 *
 * Stack-allocatable.  Must be initialized before use and destroyed
 * after the last tick completes.
 */
typedef struct phys_tick_runner {
    struct phys_world        *world;      /**< Borrowed physics world. */
    struct phys_job_context  *jobs;       /**< Borrowed job context. */
    struct fr_topic_channel  *cmd_channel;/**< Borrowed command channel (may be NULL). */

    /** Optional callback invoked for each SPAWN_BODY during drain. */
    phys_cmd_spawn_callback_t spawn_cb;
    void                     *spawn_cb_user;

    /** Atomic completion flag — set by the tick job, read by the main thread. */
    atomic_int tick_done;

    /** True while a tick job is dispatched and not yet consumed. */
    uint8_t tick_in_flight;

    /** Persistent storage for tick job arguments.  Must outlive the
     *  async dispatch since kick() returns immediately. */
    struct {
        struct phys_world       *world;
        struct phys_job_context *jobs;
    } tick_args_;
} phys_tick_runner_t;

/**
 * @brief Initialize a tick runner.
 *
 * @param r              Runner to initialize.  Must not be NULL.
 * @param world          Physics world to tick.  Must not be NULL.
 * @param jobs           Job context for parallel dispatch.  Must not be NULL.
 * @param cmd_channel    Command channel to drain (may be NULL — no drain).
 * @param spawn_cb       Callback for SPAWN_BODY results (may be NULL).
 * @param spawn_cb_user  Opaque pointer forwarded to spawn_cb.
 */
void phys_tick_runner_init(phys_tick_runner_t *r,
                           struct phys_world *world,
                           struct phys_job_context *jobs,
                           struct fr_topic_channel *cmd_channel,
                           phys_cmd_spawn_callback_t spawn_cb,
                           void *spawn_cb_user);

/**
 * @brief Tear down the runner.  No-op if NULL.
 *
 * Must only be called when no tick is in flight.
 */
void phys_tick_runner_destroy(phys_tick_runner_t *r);

/**
 * @brief Dispatch one physics tick as a non-blocking fiber job.
 *
 * The tick job drains the command channel, runs the parallel physics
 * tick, and signals completion atomically.  Returns immediately.
 *
 * Caller must ensure no tick is in flight (done + consume first).
 *
 * @param r  Runner.  Must not be NULL.
 */
void phys_tick_runner_kick(phys_tick_runner_t *r);

/**
 * @brief Non-blocking poll: has the in-flight tick completed?
 *
 * @param r  Runner (NULL-safe, returns 1).
 * @return 1 if done or no tick in flight, 0 if still running.
 */
int phys_tick_runner_done(const phys_tick_runner_t *r);

/**
 * @brief Acknowledge tick completion, allowing the next kick.
 *
 * Must be called after done() returns 1 and before the next kick().
 *
 * @param r  Runner.  Must not be NULL.
 */
void phys_tick_runner_consume(phys_tick_runner_t *r);

/**
 * @brief Block until the in-flight tick completes (shutdown only).
 *
 * Spins on the atomic flag.  No-op if no tick is in flight.
 *
 * @param r  Runner (NULL-safe).
 */
void phys_tick_runner_wait(phys_tick_runner_t *r);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_PHYS_TICK_RUNNER_H */
