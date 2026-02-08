#include <stdlib.h>
#include <sched.h>
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
        job_system_t *target_sys = sys ? sys : fiber->system;

        /* Try to transition the fiber's wait state.
         *
         * State 3 (parked-and-yielded): the fiber has safely completed its
         * context swap.  Transition 3→0 and enqueue — this is the normal path.
         *
         * State 1 (parking): the fiber hasn't finished its swap yet (or
         * run_entry hasn't observed it).  Transition 1→2 so that run_entry
         * will re-enqueue when it sees the state.  We must NOT enqueue here
         * because the fiber's context may still be mid-swap on another CPU.
         */
        int expected = 3;
        if (atomic_compare_exchange_strong_explicit(&fiber->waiting, &expected, 0,
                                                     memory_order_acq_rel,
                                                     memory_order_acquire)) {
            /* Normal wakeup: fiber was parked and yielded.  Safe to enqueue. */
            (void)job_system_enqueue(target_sys, fiber, (int)fiber->priority, 0);
        } else {
            /* expected now holds the actual value.  If it's 1, the fiber
             * is still mid-park.  Signal it so run_entry can re-enqueue. */
            expected = 1;
            (void)atomic_compare_exchange_strong_explicit(&fiber->waiting, &expected, 2,
                                                          memory_order_acq_rel,
                                                          memory_order_acquire);
            /* If this CAS also fails (state was 0 or 2), that's unusual but
             * harmless — the fiber is already running or already signalled. */
        }

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
            sched_yield();
        }
        /* Synchronize with the final decrement-to-zero path, which may still
           be detaching waiters under counter->lock even after value hits 0.
           This avoids reuse of stack-allocated counters racing the wakeup. */
        job_spinlock_lock(&counter->lock);
        job_spinlock_unlock(&counter->lock);
        return JOB_WAIT_OK;
    }

    job_spinlock_lock(&counter->lock);
    /* The counter may have reached 0 between our unlocked check above and
       acquiring the lock. If we park after it hits 0, we can miss the only
       wakeup and deadlock forever. */
    if (atomic_load_explicit(&counter->value, memory_order_relaxed) == 0) {
        job_spinlock_unlock(&counter->lock);
        return JOB_WAIT_OK;
    }
    job_counter_waiter_t *node = (job_counter_waiter_t *)malloc(sizeof(job_counter_waiter_t));
    if (!node) {
        job_spinlock_unlock(&counter->lock);
        return JOB_WAIT_INVALID;
    }
    atomic_store_explicit(&fiber->waiting, 1, memory_order_release);
    node->fiber = fiber;
    node->next = counter->waiters;
    counter->waiters = node;
    job_spinlock_unlock(&counter->lock);

    #ifdef TRACY_ENABLE
        TracyCZoneEnd(fiber->zone);
    #endif
    #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
        TracyCFiberLeave;
    #endif
    /* Verify stack canary before yielding on wait. */
    job_stack_canary_check(fiber->stack, fiber->system->fiber_stack_size,
                           fiber->id, "job_wait_counter:yield");
    fiber->swap_caller = __builtin_return_address(0);
    fiber->swap_site = "job_wait_counter:yield";
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
            sched_yield();
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

    job_spinlock_lock(&counter->lock);
    job_system_wake_waiters_locked(sys, counter);
    job_spinlock_unlock(&counter->lock);
}
