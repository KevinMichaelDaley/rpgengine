/**
 * @file phys_tick_runner.c
 * @brief Continuous physics tick runner implementation.
 *
 * Spawns a dedicated OS thread that loops forever (until stop is
 * requested).  Each iteration: pace → drain commands → tick →
 * drain corrections → signal.  Pacing uses nanosleep between ticks.
 * Parallel physics stages are dispatched to the job system's worker
 * pool from the tick thread.
 */

#define _POSIX_C_SOURCE 200809L

#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/job/system.h"

#include <pthread.h>
#include <stdatomic.h>
#include <string.h>
#include <time.h>

/* ── Persistent tick loop (runs on a dedicated OS thread) ────────── */

/** Read CLOCK_MONOTONIC in nanoseconds. */
static uint64_t runner_clock_ns_(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

/* ── Overload detection ──────────────────────────────────────────── */

/** Rolling window size for overload detection (64-tick history). */
#define OVERLOAD_WINDOW 64

/** Fraction of overrun ticks to ENTER variable-dt mode (48/64 = 75%). */
#define OVERLOAD_ON_THRESHOLD 48

/** Fraction of recent ticks that must be clean to EXIT variable-dt
 *  mode.  Uses only the most recent 8 ticks — if 6/8 are on-time,
 *  we recover quickly. */
#define OVERLOAD_OFF_WINDOW 8
#define OVERLOAD_OFF_THRESHOLD 6

/** Tolerance: a tick is only "overrun" if it exceeds target by more
 *  than 10% (accounts for nanosleep jitter). */
#define OVERLOAD_TOLERANCE_NUM 11
#define OVERLOAD_TOLERANCE_DEN 10

/** Count set bits in a 64-bit value. */
static int popcount64_(uint64_t v) {
    int c = 0;
    while (v) { c += v & 1; v >>= 1; }
    return c;
}

/** Thread entry point: loops continuously, one tick per iteration.
 *  Pacing uses nanosleep — no fiber yield needed. */
static void *tick_thread_fn_(void *user_data) {
    phys_tick_runner_t *r = (phys_tick_runner_t *)user_data;
    uint64_t last_tick_ns = 0;
    const uint64_t target_ns =
        (uint64_t)(r->world->config.fixed_dt * 1e9f);

    /* Rolling bitfield: bit i = 1 if i-th most recent tick overran. */
    uint64_t overload_history = 0;
    int in_overload = 0;

    while (!atomic_load_explicit(&r->stop_requested, memory_order_acquire)) {

        /* Pace: sleep until fixed_dt has elapsed since last tick. */
        uint64_t wall_elapsed_ns = 0;
        if (last_tick_ns != 0) {
            uint64_t now = runner_clock_ns_();
            wall_elapsed_ns = now - last_tick_ns;
            if (wall_elapsed_ns < target_ns) {
                uint64_t remain = target_ns - wall_elapsed_ns;
                struct timespec ts = {
                    .tv_sec  = (time_t)(remain / 1000000000ULL),
                    .tv_nsec = (long)(remain % 1000000000ULL)
                };
                nanosleep(&ts, NULL);
                wall_elapsed_ns = target_ns; /* slept to target */
            }
            if (atomic_load_explicit(&r->stop_requested,
                                     memory_order_acquire)) {
                break;
            }
        }

        uint64_t tick_wall_start = runner_clock_ns_();
        if (last_tick_ns != 0) {
            /* Recompute actual elapsed since last tick_wall_start, including
             * any sleep.  This is the true wall-clock interval. */
            wall_elapsed_ns = tick_wall_start - last_tick_ns;
        }
        last_tick_ns = tick_wall_start;

        /* Update overload history: shift in a 1 if we overran beyond
         * tolerance, 0 if on-time. */
        if (wall_elapsed_ns > 0) {
            uint64_t threshold_ns = (uint64_t)target_ns *
                OVERLOAD_TOLERANCE_NUM / OVERLOAD_TOLERANCE_DEN;
            int overran = (wall_elapsed_ns > threshold_ns) ? 1 : 0;
            overload_history = (overload_history << 1) | (uint64_t)overran;
        }

        /* Hysteresis: hard to enter overload, easy to leave. */
        if (!in_overload) {
            /* Need sustained overload across full window to enter. */
            int total = popcount64_(overload_history);
            if (total >= OVERLOAD_ON_THRESHOLD) {
                in_overload = 1;
            }
        } else {
            /* Check only recent ticks — recover quickly once load drops. */
            uint64_t recent = overload_history &
                ((1ULL << OVERLOAD_OFF_WINDOW) - 1);
            int recent_clean = OVERLOAD_OFF_WINDOW - popcount64_(recent);
            if (recent_clean >= OVERLOAD_OFF_THRESHOLD) {
                in_overload = 0;
            }
        }

        if (in_overload && wall_elapsed_ns > 0) {
            /* Sustained overload: use actual wall time as dt. */
            r->world->dt_override = (float)wall_elapsed_ns * 1e-9f;
        } else {
            /* Normal operation or recovery: use fixed dt. */
            r->world->dt_override = 0.0f;
        }

        /* Drain spawns/mutations before tick. */
        if (r->cmd_channel) {
            phys_cmd_drain(r->world, r->cmd_channel,
                           r->spawn_cb, r->spawn_cb_user);
        }

        /* Run one physics tick (dispatches parallel jobs to workers). */
        uint64_t tick_start_ns = runner_clock_ns_();
        phys_world_tick_parallel(r->world, r->game_state, r->jobs);
        uint64_t tick_end_ns = runner_clock_ns_();
        atomic_store_explicit(&r->last_tick_duration_ns,
                              tick_end_ns - tick_start_ns,
                              memory_order_relaxed);
        atomic_store_explicit(&r->last_tick_completion_ms,
                              tick_end_ns / 1000000u,
                              memory_order_relaxed);

        /* Drain corrections after tick. */
        if (r->correction_channel) {
            phys_cmd_drain(r->world, r->correction_channel,
                           NULL, NULL);
        }

        /* Post-tick callback (e.g. send priority state updates). */
        uint64_t next_tick = atomic_load_explicit(&r->completed_ticks,
                                                   memory_order_relaxed) + 1;
        if (r->post_tick_cb) {
            r->post_tick_cb(r->post_tick_cb_user, next_tick);
        }

        /* Signal tick completion. */
        atomic_fetch_add_explicit(&r->completed_ticks, 1,
                                  memory_order_release);
    }

    atomic_store_explicit(&r->stopped, 1, memory_order_release);
    return NULL;
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
    pthread_create(&r->thread, NULL, tick_thread_fn_, r);
}

void phys_tick_runner_stop(phys_tick_runner_t *r) {
    if (!r || !r->running) { return; }
    atomic_store_explicit(&r->stop_requested, 1, memory_order_release);
    pthread_join(r->thread, NULL);
    r->running = 0;
}

uint64_t phys_tick_runner_tick_id(const phys_tick_runner_t *r) {
    if (!r) { return 0; }
    return atomic_load_explicit(
        &((phys_tick_runner_t *)r)->completed_ticks, memory_order_acquire);
}

uint64_t phys_tick_runner_last_tick_ns(const phys_tick_runner_t *r) {
    if (!r) { return 0; }
    return atomic_load_explicit(
        &((phys_tick_runner_t *)r)->last_tick_duration_ns, memory_order_relaxed);
}

uint64_t phys_tick_runner_last_tick_completion_ms(const phys_tick_runner_t *r) {
    if (!r) { return 0; }
    return atomic_load_explicit(
        &((phys_tick_runner_t *)r)->last_tick_completion_ms, memory_order_relaxed);
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
