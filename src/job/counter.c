#include <limits.h>

#include "internal.h"

void job_counter_init(job_counter_t *counter, uint32_t initial) {
    if (!counter) {
        return;
    }
    atomic_init(&counter->value, initial);
    counter->waiters = NULL;
    mtx_init(&counter->lock, mtx_plain);
}

void job_counter_destroy(job_counter_t *counter) {
    if (!counter) {
        return;
    }
    /* Caller must ensure no concurrent access. */
    counter->waiters = NULL;
    (void)mtx_destroy(&counter->lock);
}

int job_counter_add(job_counter_t *counter, uint32_t value) {
    if (!counter) {
        return -1;
    }
    uint32_t current = atomic_load_explicit(&counter->value, memory_order_relaxed);
    if (value > UINT_MAX - current) {
        return -1;
    }
    atomic_fetch_add_explicit(&counter->value, value, memory_order_relaxed);
    return 0;
}

int job_counter_dec(job_counter_t *counter) {
    if (!counter) {
        return -1;
    }
    for (;;) {
        uint32_t current = atomic_load_explicit(&counter->value, memory_order_relaxed);
        if (current == 0) {
            return -1;
        }

        /* When transitioning 1 -> 0, synchronize with counter->lock so a
           waiter observing value==0 can't race reuse/destruction with the
           wake-up path (which also uses counter->lock). */
        if (current == 1u) {
            mtx_lock(&counter->lock);
            current = atomic_load_explicit(&counter->value, memory_order_relaxed);
            if (current == 0u) {
                mtx_unlock(&counter->lock);
                return -1;
            }
            if (current == 1u) {
                /* Use an acq_rel RMW for the final 1->0 transition so this
                   decrement participates in the atomic modification chain.
                   This ensures a waiter that observes 0 can also observe all
                   job-side writes that happened-before earlier decrements. */
                (void)atomic_fetch_sub_explicit(&counter->value, 1u, memory_order_acq_rel);
                job_system_wake_waiters_locked(NULL, counter);
                mtx_unlock(&counter->lock);
                return 0;
            }
            mtx_unlock(&counter->lock);
            continue;
        }

        if (atomic_compare_exchange_weak_explicit(&counter->value, &current, current - 1,
                                                  memory_order_acq_rel, memory_order_relaxed)) {
            if (current - 1 == 0) {
                job_system_wake_waiters(NULL, counter);
            }
            return 0;
        }
    }
}

uint32_t job_counter_value(const job_counter_t *counter) {
    if (!counter) {
        return 0;
    }
    return atomic_load_explicit(&counter->value, memory_order_relaxed);
}
