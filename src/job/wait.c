#include <stdlib.h>

#include "internal.h"

typedef struct job_counter_waiter {
    job_fiber_t *fiber;
    struct job_counter_waiter *next;
} job_counter_waiter_t;

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
        return JOB_WAIT_OK;
    }

    mtx_lock(&counter->lock);
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

    swapcontext(&fiber->ctx, g_scheduler_context);
    return JOB_WAIT_OK;
}

void job_system_wake_waiters(job_system_t *sys, job_counter_t *counter) {
    if (!counter) {
        return;
    }

    mtx_lock(&counter->lock);
    job_counter_waiter_t *cursor = counter->waiters;
    counter->waiters = NULL;
    mtx_unlock(&counter->lock);

    while (cursor) {
        job_counter_waiter_t *next = cursor->next;
        job_fiber_t *fiber = cursor->fiber;
        fiber->waiting = 0;
        job_system_t *target_sys = sys ? sys : fiber->system;
        job_system_enqueue(target_sys, fiber, (int)fiber->priority, 0);
        free(cursor);
        cursor = next;
    }
}
