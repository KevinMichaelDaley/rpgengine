#include <limits.h>
#include <stdlib.h>

#include "internal.h"

int job_system_enqueue(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id) {
    if (!sys || !fiber) {
        return -1;
    }

    mtx_lock(&sys->queue_lock);
    if (sys->queue_size >= sys->queue_capacity) {
        mtx_unlock(&sys->queue_lock);
        return -1;
    }

    uint32_t pos = (sys->queue_head + sys->queue_size) % sys->queue_capacity;
    sys->queue[pos].fiber = fiber;
    sys->queue[pos].priority = priority;
    sys->queue[pos].id = id;
    sys->queue_size++;
    cnd_broadcast(&sys->queue_cond);
    mtx_unlock(&sys->queue_lock);
    return 0;
}

int job_system_pop_next(job_system_t *sys, struct job_entry *out_entry) {
    if (!sys || !out_entry) {
        return -1;
    }

    mtx_lock(&sys->queue_lock);
    while (sys->queue_size == 0 && !atomic_load(&sys->shutting_down)) {
        if (sys->deterministic) {
            mtx_unlock(&sys->queue_lock);
            return -1;
        }
        cnd_wait(&sys->queue_cond, &sys->queue_lock);
    }

    if (sys->queue_size == 0) {
        mtx_unlock(&sys->queue_lock);
        return -1;
    }

    uint32_t best_offset = 0;
    int best_priority = INT_MIN;
    for (uint32_t i = 0; i < sys->queue_size; ++i) {
        uint32_t idx = (sys->queue_head + i) % sys->queue_capacity;
        if (sys->queue[idx].priority > best_priority) {
            best_priority = sys->queue[idx].priority;
            best_offset = i;
        }
    }

    uint32_t abs_idx = (sys->queue_head + best_offset) % sys->queue_capacity;
    *out_entry = sys->queue[abs_idx];

    for (uint32_t i = best_offset; i + 1 < sys->queue_size; ++i) {
        uint32_t from_idx = (sys->queue_head + i + 1) % sys->queue_capacity;
        uint32_t to_idx = (sys->queue_head + i) % sys->queue_capacity;
        sys->queue[to_idx] = sys->queue[from_idx];
    }

    sys->queue_size--;
    sys->queue_tail = (sys->queue_head + sys->queue_size) % sys->queue_capacity;
    mtx_unlock(&sys->queue_lock);
    return 0;
}
