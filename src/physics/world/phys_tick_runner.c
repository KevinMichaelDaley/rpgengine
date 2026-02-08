/**
 * @file phys_tick_runner.c
 * @brief Async physics tick runner implementation.
 *
 * Provides the kick/poll/consume lifecycle for running physics ticks
 * as non-blocking fiber jobs.  The tick job drains the command channel,
 * runs phys_world_tick_parallel(), and signals completion atomically.
 */

#include "ferrum/physics/phys_tick_runner.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/tick.h"
#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_cmd.h"
#include "ferrum/net/topic_channel.h"
#include "ferrum/job/system.h"

#include <stdatomic.h>
#include <string.h>

/* ── Tick job function (runs on a fiber) ─────────────────────────── */

/** Job function dispatched by kick().  Drains spawn commands, runs the
 *  parallel tick, drains corrections, and signals completion. */
static void tick_runner_job_fn_(void *user_data) {
    phys_tick_runner_t *r = (phys_tick_runner_t *)user_data;

    /* Drain spawns/mutations before tick so new bodies exist for sim. */
    if (r->cmd_channel) {
        phys_cmd_drain(r->tick_args_.world, r->cmd_channel,
                       r->spawn_cb, r->spawn_cb_user);
    }

    phys_world_tick_parallel(r->tick_args_.world, NULL, r->tick_args_.jobs);

    /* Drain corrections after tick so they override simulated state. */
    if (r->correction_channel) {
        phys_cmd_drain(r->tick_args_.world, r->correction_channel,
                       NULL, NULL);
    }

    atomic_store_explicit(&r->tick_done, 1, memory_order_release);
}

/* ── Public API (4 non-static functions) ─────────────────────────── */

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

    atomic_init(&r->tick_done, 1); /* No tick in flight initially. */
    r->tick_in_flight = 0;
}

void phys_tick_runner_destroy(phys_tick_runner_t *r) {
    (void)r; /* Nothing to free — we borrow everything. */
}

void phys_tick_runner_kick(phys_tick_runner_t *r) {
    if (!r || !r->world || !r->jobs) { return; }

    atomic_store_explicit(&r->tick_done, 0, memory_order_release);
    r->tick_in_flight = 1;

    r->tick_args_.world = r->world;
    r->tick_args_.jobs  = r->jobs;

    job_dispatch_named(r->jobs->job_sys, tick_runner_job_fn_, r,
                       0, NULL, "Phys.Tick.Parallel");
}

int phys_tick_runner_done(const phys_tick_runner_t *r) {
    if (!r || !r->tick_in_flight) { return 1; }
    return atomic_load_explicit(
        &((phys_tick_runner_t *)r)->tick_done, memory_order_acquire);
}

void phys_tick_runner_consume(phys_tick_runner_t *r) {
    if (!r) { return; }
    r->tick_in_flight = 0;
}

void phys_tick_runner_wait(phys_tick_runner_t *r) {
    if (!r || !r->tick_in_flight) { return; }
    while (!atomic_load_explicit(&r->tick_done, memory_order_acquire)) {
        /* spin — shutdown only */
    }
    r->tick_in_flight = 0;
}
