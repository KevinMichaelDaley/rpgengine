#include <stdlib.h>

#include <string.h>

#include "internal.h"

typedef struct job_counter_waiter {
    job_fiber_t *fiber;
    struct job_counter_waiter *next;
} job_counter_waiter_t;

void job_system_wake_waiters_locked(job_system_t *sys, job_counter_t *counter) {
    if (!counter) {
        return;
    }

    job_counter_waiter_t *cursor = (job_counter_waiter_t *)counter->waiters;
    counter->waiters = NULL;

    while (cursor) {
        job_counter_waiter_t *next = cursor->next;
        job_fiber_t *fiber = cursor->fiber;
        fiber->waiting = 0;
        job_system_t *target_sys = sys ? sys : fiber->system;
        (void)job_system_enqueue(target_sys, fiber, (int)fiber->priority, 0);
        free(cursor);
        cursor = next;
    }
}

job_wait_status_t job_wait_counter(job_counter_t *counter, uint32_t spin_count) {
    if (!counter) {
        return JOB_WAIT_INVALID;
    }

    for (uint32_t i = 0; i < spin_count; ++i) {
        if (atomic_load_explicit(&counter->value, memory_order_relaxed) == 0) {
            return JOB_WAIT_OK;
        }
    }

    if (atomic_load_explicit(&counter->value, memory_order_relaxed) == 0) {
        return JOB_WAIT_OK;
    }

    job_fiber_t *fiber = g_current_fiber;
    if (!fiber || !g_scheduler_context) {
        while (atomic_load_explicit(&counter->value, memory_order_acquire) != 0) {
            thrd_yield();
        }
        /* Synchronize with the final decrement-to-zero path, which may still
           be detaching waiters under counter->lock even after value hits 0.
           This avoids reuse of stack-allocated counters racing the wakeup. */
        mtx_lock(&counter->lock);
        mtx_unlock(&counter->lock);
        return JOB_WAIT_OK;
    }

    mtx_lock(&counter->lock);
    /* The counter may have reached 0 between our unlocked check above and
       acquiring the lock. If we park after it hits 0, we can miss the only
       wakeup and deadlock forever. */
    if (atomic_load_explicit(&counter->value, memory_order_relaxed) == 0) {
        mtx_unlock(&counter->lock);
        return JOB_WAIT_OK;
    }
    job_counter_waiter_t *node = (job_counter_waiter_t *)malloc(sizeof(job_counter_waiter_t));
    if (!node) {
        mtx_unlock(&counter->lock);
        return JOB_WAIT_INVALID;
    }
    fiber->waiting = 1;
    node->fiber = fiber;
    node->next = counter->waiters;
    counter->waiters = node;
    mtx_unlock(&counter->lock);

    #ifdef TRACY_ENABLE
        TracyCZoneEnd(fiber->zone);
    #endif
    #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
        TracyCFiberLeave;
    #endif
    job_context_swap(&fiber->ctx, g_scheduler_context);
    #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
        if (fiber->tracy_name) {
            TracyCFiberEnter(fiber->tracy_name);
        }
    #endif
    #ifdef TRACY_ENABLE
        const char *zone_name = fiber->tracy_name ? fiber->tracy_name : "unnamed_fiber";
        TracyCZone(zone, true);
        TracyCZoneName(zone, zone_name, strlen(zone_name));
        fiber->zone = zone;
    #endif
    for (;;) {
        if (atomic_load_explicit(&counter->value, memory_order_relaxed) == 0) {
            break;
        }
        if (!g_scheduler_context || !g_current_system) {
            thrd_yield();
            continue;
        }
        struct job_entry entry;
        while (job_system_pop_next(g_current_system, &entry) == 0) {
            run_entry(g_current_system, &entry, g_scheduler_context);
        }
    }
    return JOB_WAIT_OK;
}

void job_system_wake_waiters(job_system_t *sys, job_counter_t *counter) {
    if (!counter) {
        return;
    }

    mtx_lock(&counter->lock);
    job_system_wake_waiters_locked(sys, counter);
    mtx_unlock(&counter->lock);
}
