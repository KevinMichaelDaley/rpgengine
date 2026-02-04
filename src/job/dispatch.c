#include "internal.h"

#include <string.h>

job_id_t job_dispatch(job_system_t *sys,
                     void (*fn)(void *user_data),
                     void *user_data,
                     int priority,
                     job_counter_t *counter) {
    if (!sys || !fn || !atomic_load(&sys->running)) {
        return JOB_ID_INVALID;
    }
    if (atomic_load(&sys->shutting_down)) {
        return JOB_ID_INVALID;
    }

    if (counter && job_counter_add(counter, 1) != 0) {
        return JOB_ID_INVALID;
    }

    uint64_t id = atomic_fetch_add_explicit(&sys->next_job_id, 1, memory_order_relaxed);
    job_fiber_t *fiber = job_fiber_create(sys, fn, user_data, counter, priority, id);
    if (!fiber) {
        if (counter) {
            job_counter_dec(counter);
        }
        return JOB_ID_INVALID;
    }

    if (job_system_enqueue(sys, fiber, priority, id) != 0) {
        job_fiber_destroy(fiber);
        if (counter) {
            job_counter_dec(counter);
        }
        return JOB_ID_INVALID;
    }
    atomic_fetch_add_explicit(&sys->jobs_started, 1, memory_order_release);
    job_instrument_event("dispatch", fiber->id, id, g_worker_id, __FILE__, __LINE__);
    return id;
}

void job_yield(void) {
    if (!g_current_fiber || !g_scheduler_context) {
        return;
    }
    #ifdef TRACY_ENABLE
        TracyCZoneEnd(g_current_fiber->zone);
    #endif
    #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
        TracyCFiberLeave;
    #endif
    job_context_swap(&g_current_fiber->ctx, g_scheduler_context);
    #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
        if (g_current_fiber->tracy_name) {
            TracyCFiberEnter(g_current_fiber->tracy_name);
        }
    #endif

    #ifdef TRACY_ENABLE
        const char *zone_name = g_current_fiber->tracy_name ? g_current_fiber->tracy_name : "unnamed_fiber";
        TracyCZone(zone, true);
        TracyCZoneName(zone, zone_name, strlen(zone_name));
        g_current_fiber->zone = zone;
    #endif
}

uint32_t job_current_worker_id(void) {
    return g_worker_id;
}

uint32_t job_current_worker_node(void) {
    return g_worker_node;
}
