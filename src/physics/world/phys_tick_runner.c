/**
 * @file phys_tick_runner.c
 * @brief Continuous physics tick runner implementation.
 *
 * Dispatches a single persistent fiber that loops forever (until
 * stop is requested).  Each iteration: pace → drain commands → tick →
 * drain corrections → signal.  Pacing yields the fiber between ticks
 * so other fibers on the same job system can run.
 */

#define _POSIX_C_SOURCE 200809L

#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/job/system.h"

#include <stdatomic.h>
#include <string.h>

/* ── Persistent tick loop (runs on a fiber) ──────────────────────── */

#include <time.h>

/** Read CLOCK_MONOTONIC in nanoseconds. */
static uint64_t runner_clock_ns_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/** Fiber job function: loops continuously, one tick per iteration.
 *  Pacing is handled here — tick functions run without internal waits. */
static void tick_loop_fn_(void *user_data) {
    phys_tick_runner_t *r = (phys_tick_runner_t *)user_data;
    uint64_t last_tick_ns = 0;
    const uint64_t target_ns =
        (uint64_t)(r->world->config.fixed_dt * 1e9f);

    while (!atomic_load_explicit(&r->stop_requested, memory_order_acquire)) {

        /* Pace: yield until fixed_dt has elapsed since last tick. */
        if (last_tick_ns != 0) {
            while (runner_clock_ns_() - last_tick_ns < target_ns) {
                if (atomic_load_explicit(&r->stop_requested,
                                         memory_order_acquire)) {
                    goto done;
                }
                job_yield();
            }
        }
        last_tick_ns = runner_clock_ns_();

        /* Drain spawns/mutations before tick. */
        if (r->cmd_channel) {
            phys_cmd_drain(r->world, r->cmd_channel,
                           r->spawn_cb, r->spawn_cb_user);
        }

        /* Run one physics tick. */
        phys_world_tick_parallel(r->world, r->game_state, r->jobs);

        /* Drain corrections after tick. */
        if (r->correction_channel) {
            phys_cmd_drain(r->world, r->correction_channel,
                           NULL, NULL);
        }

        /* Signal tick completion. */
        atomic_fetch_add_explicit(&r->completed_ticks, 1,
                                  memory_order_release);
    }

done:
    atomic_store_explicit(&r->stopped, 1, memory_order_release);
}

/* ── Public API ──────────────────────────────────────────────────── */

void phys_tick_runner_init(phys_tick_runner_t *r,
                           phys_world_t *world,
                           phys_job_context_t *jobs,
                           fr_topic_channel_t *cmd_channel,
                           fr_topic_channel_t *correction_channel,
                           phys_cmd_spawn_callback_t spawn_cb,
                           void *spawn_cb_user) {
    if (!r) { return; }
    memset(r, 0, sizeof(*r));

    r->world              = world;
    r->jobs               = jobs;
    r->cmd_channel        = cmd_channel;
    r->correction_channel = correction_channel;
    r->spawn_cb           = spawn_cb;
    r->spawn_cb_user      = spawn_cb_user;

    atomic_init(&r->completed_ticks, 0);
    atomic_init(&r->stop_requested, 0);
    atomic_init(&r->stopped, 0);
    r->running = 0;
}

void phys_tick_runner_destroy(phys_tick_runner_t *r) {
    (void)r; /* Nothing to free — we borrow everything. */
}

void phys_tick_runner_start(phys_tick_runner_t *r) {
    if (!r || !r->world || !r->jobs || r->running) { return; }
    r->running = 1;
    job_dispatch_named(r->jobs->job_sys, tick_loop_fn_, r,
                       0, NULL, "Phys.Tick.Loop");
}

static void noop_wakeup_job_(void *user_data) { (void)user_data; }

void phys_tick_runner_stop(phys_tick_runner_t *r) {
    if (!r || !r->running) { return; }
    atomic_store_explicit(&r->stop_requested, 1, memory_order_release);
    while (!atomic_load_explicit(&r->stopped, memory_order_acquire)) {
        /* Dispatch a no-op job to wake a sleeping worker so the tick
         * fiber gets a chance to be resumed and observe stop_requested. */
        job_dispatch_named(r->jobs->job_sys, noop_wakeup_job_, NULL,
                           0, NULL, "Phys.Tick.StopWakeup");
        struct timespec ts = { .tv_sec = 0, .tv_nsec = 1000000 }; /* 1 ms */
        nanosleep(&ts, NULL);
    }
    r->running = 0;
}

uint64_t phys_tick_runner_tick_id(const phys_tick_runner_t *r) {
    if (!r) { return 0; }
    return atomic_load_explicit(
        &((phys_tick_runner_t *)r)->completed_ticks, memory_order_acquire);
}

/* ── Backward compatibility wrappers ─────────────────────────────── */

void phys_tick_runner_kick(phys_tick_runner_t *r) {
    if (!r) { return; }
    if (!r->running) {
        phys_tick_runner_start(r);
    }
}

int phys_tick_runner_done(const phys_tick_runner_t *r) {
    (void)r;
    return 1; /* Ticks complete continuously. */
}

void phys_tick_runner_consume(phys_tick_runner_t *r) {
    (void)r; /* No-op. */
}

void phys_tick_runner_wait(phys_tick_runner_t *r) {
    phys_tick_runner_stop(r);
}
