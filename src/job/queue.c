#include <limits.h>
#include <stdlib.h>

#include "internal.h"

/* Slot state values: 0 = empty, 1 = ready (has job), 2 = busy (claimed) */

int job_system_enqueue(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id) {
    if (!sys || !fiber) {
        return -1;
    }
    /* Deterministic mode and non-worker contexts should append using global insert cursor.
       Otherwise, prefer local region based on current worker id. */
    uint32_t start;
    if (sys->deterministic || g_worker_id == UINT32_MAX) {
        start = atomic_fetch_add_explicit(&sys->queue_insert_cursor, 1u, memory_order_relaxed) % sys->queue_capacity;
    } else {
        start = g_worker_id % sys->queue_capacity;
    }
    for (uint32_t off = 0; off < sys->queue_capacity; ++off) {
        uint32_t i = (start + off) % sys->queue_capacity;
        int expected = 0;
        if (atomic_compare_exchange_strong_explicit(&sys->queue_slot_state[i], &expected, 2,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            sys->queue[i].fiber = fiber;
            sys->queue[i].priority = priority;
            sys->queue[i].id = id;
            atomic_store_explicit(&sys->queue_slot_state[i], 1, memory_order_release);
            job_instrument_event("enqueue", fiber->id, id, g_worker_id, __FILE__, __LINE__);
            cnd_broadcast(&sys->queue_cond);
            return 0;
        }
    }
    return -1;
}

int job_system_pop_next(job_system_t *sys, struct job_entry *out_entry) {
    if (!sys || !out_entry) {
        return -1;
    }
    /* Deterministic mode should rotate scan start via global pop cursor to preserve fairness.
       Non-deterministic workers prefer local region to enable sharding + stealing. */
    uint32_t start;
    if (sys->deterministic || g_worker_id == UINT32_MAX) {
        start = atomic_fetch_add_explicit(&sys->queue_pop_cursor, 1u, memory_order_relaxed) % sys->queue_capacity;
    } else {
        start = g_worker_id % sys->queue_capacity;
    }
    for (uint32_t attempt = 0; attempt < sys->queue_capacity; ++attempt) {
        int chosen_idx = -1;
        int best_priority = INT_MIN;
        for (uint32_t off = 0; off < sys->queue_capacity; ++off) {
            uint32_t i = (start + off) % sys->queue_capacity;
            int state = atomic_load_explicit(&sys->queue_slot_state[i], memory_order_acquire);
            if (state == 1) {
                if (!sys->deterministic) {
                    chosen_idx = (int)i; /* pick first READY to reduce contention */
                    break;
                } else {
                    int p = sys->queue[i].priority;
                    /* Highest priority wins; ties favor first encountered from rotated scan */
                    if (chosen_idx < 0 || p > best_priority) {
                        best_priority = p;
                        chosen_idx = (int)i;
                    }
                }
            }
        }

        if (chosen_idx < 0) {
            return -1;
        }

        int expected = 1;
        if (atomic_compare_exchange_strong_explicit(&sys->queue_slot_state[chosen_idx], &expected, 2,
                                                    memory_order_acq_rel, memory_order_acquire)) {
            *out_entry = sys->queue[chosen_idx];
            job_instrument_event("pop", out_entry->fiber ? out_entry->fiber->id : 0, out_entry->id, g_worker_id, __FILE__, __LINE__);
            atomic_store_explicit(&sys->queue_slot_state[chosen_idx], 0, memory_order_release);
            return 0;
        }
        /* CAS failed due to contention; retry selection. */
    }
    return -1;
}
