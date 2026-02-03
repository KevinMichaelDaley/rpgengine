#include <limits.h>
#include <stdint.h>

#include "internal.h"

/* Deterministic scheduler uses a fixed slot-state queue to preserve existing ordering/priority semantics.
   Non-deterministic uses per-worker Chase–Lev work-stealing deques (no O(queue_capacity) scan). */

static int enqueue_deterministic(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id) {
    uint32_t start = atomic_fetch_add_explicit(&sys->queue_insert_cursor, 1u, memory_order_relaxed) % sys->queue_capacity;
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
            return 0;
        }
    }
    return -1;
}

static int enqueue_ws(job_system_t *sys, job_fiber_t *fiber, uint32_t preferred_worker) {
    uint32_t target;
    if (preferred_worker != UINT32_MAX) {
        target = preferred_worker % sys->worker_count;
    } else if (g_worker_id != UINT32_MAX) {
        target = g_worker_id % sys->worker_count;
    } else {
        target = atomic_fetch_add_explicit(&sys->queue_insert_cursor, 1u, memory_order_relaxed) % sys->worker_count;
    }

    /* Chase–Lev requires single-producer push/pop from the owning worker.
       Any non-owner enqueue (including dispatch from non-worker threads) goes through a locked injection ring. */
    if (g_worker_id != UINT32_MAX && (g_worker_id % sys->worker_count) == target) {
        if (fr_ws_deque_push(&sys->ws_deques[target], (void *)fiber) != 0) {
            return -1;
        }

        job_instrument_event("enqueue", fiber->id, fiber->id, g_worker_id, __FILE__, __LINE__);
        return 0;
    }

    if (sys->inject_count >= sys->queue_capacity) {
        return -1;
    }
    sys->inject_ring[sys->inject_tail] = fiber;
    sys->inject_tail = (sys->inject_tail + 1u) % sys->queue_capacity;
    sys->inject_count++;
    job_instrument_event("inject", fiber->id, fiber->id, g_worker_id, __FILE__, __LINE__);
    return 0;
}

static void *try_pop_injected(job_system_t *sys) {
    if (!sys || !sys->inject_ring) {
        return NULL;
    }

    void *p = NULL;
    mtx_lock(&sys->queue_lock);
    if (sys->inject_count > 0) {
        p = (void *)sys->inject_ring[sys->inject_head];
        sys->inject_ring[sys->inject_head] = NULL;
        sys->inject_head = (sys->inject_head + 1u) % sys->queue_capacity;
        sys->inject_count--;
    }
    mtx_unlock(&sys->queue_lock);
    return p;
}

int job_system_enqueue_preferred(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id, uint32_t preferred_worker) {
    if (!sys || !fiber) {
        return -1;
    }

#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    atomic_fetch_add_explicit(&sys->qdiag_enqueue_calls, 1, memory_order_relaxed);
#endif

    (void)priority;
    (void)id;

    /* Enqueue and signal under queue_lock so worker wait can't miss the 0->1 transition. */
    mtx_lock(&sys->queue_lock);
    unsigned int prev = atomic_fetch_add_explicit(&sys->queued_count, 1u, memory_order_relaxed);
    if (prev >= sys->queue_capacity) {
        (void)atomic_fetch_sub_explicit(&sys->queued_count, 1u, memory_order_relaxed);
        mtx_unlock(&sys->queue_lock);
        return -1;
    }

    int rc = sys->deterministic ? enqueue_deterministic(sys, fiber, (int)fiber->priority, fiber->id)
                                : enqueue_ws(sys, fiber, preferred_worker);
    if (rc != 0) {
        (void)atomic_fetch_sub_explicit(&sys->queued_count, 1u, memory_order_relaxed);
        mtx_unlock(&sys->queue_lock);
        return -1;
    }

    cnd_broadcast(&sys->queue_cond);
    mtx_unlock(&sys->queue_lock);

#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    atomic_fetch_add_explicit(&sys->qdiag_enqueue_scanned_slots, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&sys->qdiag_enqueue_success, 1, memory_order_relaxed);
#endif
    return 0;
}

int job_system_enqueue(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id) {
    return job_system_enqueue_preferred(sys, fiber, priority, id, UINT32_MAX);
}

int job_system_pop_next(job_system_t *sys, struct job_entry *out_entry) {
    if (!sys || !out_entry) {
        return -1;
    }

#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    atomic_fetch_add_explicit(&sys->qdiag_pop_calls, 1, memory_order_relaxed);
#endif

    if (sys->deterministic) {
        uint32_t start = atomic_fetch_add_explicit(&sys->queue_pop_cursor, 1u, memory_order_relaxed) % sys->queue_capacity;
        int chosen_idx = -1;
        int best_priority = INT_MIN;
        for (uint32_t off = 0; off < sys->queue_capacity; ++off) {
            uint32_t i = (start + off) % sys->queue_capacity;
            int state = atomic_load_explicit(&sys->queue_slot_state[i], memory_order_acquire);
            if (state != 1) {
                continue;
            }
            int p = sys->queue[i].priority;
            if (chosen_idx < 0 || p > best_priority) {
                best_priority = p;
                chosen_idx = (int)i;
            }
        }

        if (chosen_idx < 0) {
            return -1;
        }

        int expected = 1;
        if (!atomic_compare_exchange_strong_explicit(&sys->queue_slot_state[chosen_idx], &expected, 2,
                                                     memory_order_acq_rel, memory_order_acquire)) {
            return -1;
        }

        *out_entry = sys->queue[chosen_idx];
        atomic_store_explicit(&sys->queue_slot_state[chosen_idx], 0, memory_order_release);
        (void)atomic_fetch_sub_explicit(&sys->queued_count, 1u, memory_order_relaxed);

        job_instrument_event("pop", out_entry->fiber ? out_entry->fiber->id : 0, out_entry->id, g_worker_id, __FILE__, __LINE__);
#ifdef FR_JOB_QUEUE_DIAGNOSTICS
        atomic_fetch_add_explicit(&sys->qdiag_pop_scanned_slots, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&sys->qdiag_pop_ready_seen, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&sys->qdiag_pop_success, 1, memory_order_relaxed);
#endif
        return 0;
    }

    uint32_t wid = (g_worker_id != UINT32_MAX)
                       ? (g_worker_id % sys->worker_count)
                       : (atomic_fetch_add_explicit(&sys->queue_pop_cursor, 1u, memory_order_relaxed) % sys->worker_count);

    /* IMPORTANT: injected work (enqueued from non-owner threads) must be
       serviced even when local deques are non-empty.
       Otherwise, long-lived fibers can keep a worker deque perpetually non-empty
       and starve the injection ring forever.
     */
    void *p = try_pop_injected(sys);
    if (p) {
        job_fiber_t *fiber = (job_fiber_t *)p;
        out_entry->fiber = fiber;
        out_entry->priority = (int)fiber->priority;
        out_entry->id = fiber->id;

        (void)atomic_fetch_sub_explicit(&sys->queued_count, 1u, memory_order_relaxed);
        job_instrument_event("pop", fiber->id, fiber->id, g_worker_id, __FILE__, __LINE__);

#ifdef FR_JOB_QUEUE_DIAGNOSTICS
        atomic_fetch_add_explicit(&sys->qdiag_pop_scanned_slots, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&sys->qdiag_pop_success, 1, memory_order_relaxed);
#endif
        return 0;
    }

    /* Fairness: long-lived fibers yield frequently and are re-enqueued.
       Using LIFO owner-pop can repeatedly run the most recently yielded fiber
       and starve older fibers on the same worker. Prefer top-pop (FIFO-like)
       to keep all runnable fibers making progress.
     */
    p = fr_ws_deque_steal(&sys->ws_deques[wid]);
    if (!p) {
        p = fr_ws_deque_pop(&sys->ws_deques[wid]);
    }
    if (!p) {
        uint32_t rot = atomic_fetch_add_explicit(&sys->queue_pop_cursor, 1u, memory_order_relaxed);
        for (uint32_t off = 1; off < sys->worker_count + 1u; ++off) {
            uint32_t victim = (wid + off + rot) % sys->worker_count;
            if (victim == wid) {
                continue;
            }
            p = fr_ws_deque_steal(&sys->ws_deques[victim]);
            if (p) {
                break;
            }
        }
    }

    if (!p) {
        return -1;
    }

    job_fiber_t *fiber = (job_fiber_t *)p;
    out_entry->fiber = fiber;
    out_entry->priority = (int)fiber->priority;
    out_entry->id = fiber->id;

    (void)atomic_fetch_sub_explicit(&sys->queued_count, 1u, memory_order_relaxed);
    job_instrument_event("pop", fiber->id, fiber->id, g_worker_id, __FILE__, __LINE__);

#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    atomic_fetch_add_explicit(&sys->qdiag_pop_scanned_slots, 1, memory_order_relaxed);
    atomic_fetch_add_explicit(&sys->qdiag_pop_success, 1, memory_order_relaxed);
#endif
    return 0;
}
