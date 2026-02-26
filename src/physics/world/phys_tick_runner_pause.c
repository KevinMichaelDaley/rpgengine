/**
 * @file phys_tick_runner_pause.c
 * @brief Pause, resume, and single-step control for the tick runner.
 *
 * These functions set atomic flags that the tick thread checks each
 * iteration.  All are safe to call from any thread.
 */

#include "ferrum/physics/phys_tick_runner.h"
#include <stdatomic.h>

/* ── phys_tick_runner_pause ───────────────────────────────────────── */

void phys_tick_runner_pause(phys_tick_runner_t *r) {
    if (!r) { return; }
    atomic_store_explicit(&r->paused, 1, memory_order_release);
}

/* ── phys_tick_runner_resume ──────────────────────────────────────── */

void phys_tick_runner_resume(phys_tick_runner_t *r) {
    if (!r) { return; }
    /* Clear any pending step requests when resuming. */
    atomic_store_explicit(&r->step_requested, 0, memory_order_release);
    atomic_store_explicit(&r->paused, 0, memory_order_release);
}

/* ── phys_tick_runner_step_once ────────────────────────────────────── */

void phys_tick_runner_step_once(phys_tick_runner_t *r) {
    if (!r) { return; }
    /* Only meaningful when paused — the tick thread checks this. */
    atomic_fetch_add_explicit(&r->step_requested, 1, memory_order_release);
}

/* ── phys_tick_runner_is_paused ────────────────────────────────────── */

bool phys_tick_runner_is_paused(const phys_tick_runner_t *r) {
    if (!r) { return false; }
    return atomic_load_explicit(
        &((phys_tick_runner_t *)r)->paused, memory_order_acquire) != 0;
}
