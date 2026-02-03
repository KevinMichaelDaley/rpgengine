#include <string.h>

#include "ferrum/job/system.h"

int job_system_queue_diag_supported(void) {
#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    return 1;
#else
    return 0;
#endif
}

void job_system_queue_diag_snapshot(const job_system_t *sys, job_queue_diag_snapshot_t *out) {
    if (!out) {
        return;
    }

    memset(out, 0, sizeof(*out));

#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    if (!sys) {
        return;
    }

    out->enqueue_calls = atomic_load_explicit(&sys->qdiag_enqueue_calls, memory_order_relaxed);
    out->enqueue_scanned_slots = atomic_load_explicit(&sys->qdiag_enqueue_scanned_slots, memory_order_relaxed);
    out->enqueue_claim_fail = atomic_load_explicit(&sys->qdiag_enqueue_claim_fail, memory_order_relaxed);
    out->enqueue_success = atomic_load_explicit(&sys->qdiag_enqueue_success, memory_order_relaxed);

    out->pop_calls = atomic_load_explicit(&sys->qdiag_pop_calls, memory_order_relaxed);
    out->pop_scanned_slots = atomic_load_explicit(&sys->qdiag_pop_scanned_slots, memory_order_relaxed);
    out->pop_ready_seen = atomic_load_explicit(&sys->qdiag_pop_ready_seen, memory_order_relaxed);
    out->pop_claim_fail = atomic_load_explicit(&sys->qdiag_pop_claim_fail, memory_order_relaxed);
    out->pop_success = atomic_load_explicit(&sys->qdiag_pop_success, memory_order_relaxed);

    out->cond_waits = atomic_load_explicit(&sys->qdiag_cond_waits, memory_order_relaxed);
#else
    (void)sys;
#endif
}

void job_system_queue_diag_reset(job_system_t *sys) {
#ifdef FR_JOB_QUEUE_DIAGNOSTICS
    if (!sys) {
        return;
    }

    atomic_store_explicit(&sys->qdiag_enqueue_calls, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_enqueue_scanned_slots, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_enqueue_claim_fail, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_enqueue_success, 0, memory_order_relaxed);

    atomic_store_explicit(&sys->qdiag_pop_calls, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_pop_scanned_slots, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_pop_ready_seen, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_pop_claim_fail, 0, memory_order_relaxed);
    atomic_store_explicit(&sys->qdiag_pop_success, 0, memory_order_relaxed);

    atomic_store_explicit(&sys->qdiag_cond_waits, 0, memory_order_relaxed);
#else
    (void)sys;
#endif
}
