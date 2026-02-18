#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include "internal.h"

_Thread_local job_fiber_t *g_current_fiber = NULL;
_Thread_local job_system_t *g_current_system = NULL;
_Thread_local job_context_t *g_scheduler_context = NULL;
_Thread_local uint32_t g_worker_id = UINT32_MAX;
_Thread_local uint32_t g_worker_node = 0;

#ifdef TRACY_ENABLE
static void job_fiber_set_tracy_name(job_fiber_t *fiber, const char *debug_name) {
    if (!fiber) {
        return;
    }

    if (debug_name && debug_name[0] != '\0') {
        (void)snprintf(fiber->tracy_name_storage,
                       sizeof(fiber->tracy_name_storage),
                       "%s",
                       debug_name);
    } else {
        (void)snprintf(fiber->tracy_name_storage,
                       sizeof(fiber->tracy_name_storage),
                       "fiber.%llu",
                       (unsigned long long)fiber->id);
    }

    fiber->tracy_name = fiber->tracy_name_storage;
}
#endif

static void job_fiber_trampoline_body(job_fiber_t *fiber) {
    g_current_fiber = fiber;
    g_current_system = fiber->system;

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

    assert(fiber->magic1==0xf183);
    assert(fiber->magic2==0x3a7f);
    fiber->fn(fiber->user);
    fiber->finished = 1;
    assert(fiber->magic1==0xf183);
    assert(fiber->magic2==0x3a7f);

    if (fiber->counter) {
        job_counter_dec(fiber->counter);
    }
    atomic_fetch_add_explicit(&fiber->system->jobs_completed, 1, memory_order_acq_rel);

    pthread_mutex_lock(&fiber->system->sleep_mtx);
    pthread_cond_broadcast(&fiber->system->sleep_cnd);
    pthread_mutex_unlock(&fiber->system->sleep_mtx);

    for (;;) {

        #ifdef TRACY_ENABLE
            TracyCZoneEnd(fiber->zone);
        #endif

        #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
            TracyCFiberLeave;
        #endif
	assert(fiber->magic1==0xf183);
    	assert(fiber->magic2==0x3a7f);
        /* Verify stack canary before yielding back to the scheduler. */
        job_stack_canary_check(fiber->stack, fiber->system->fiber_stack_size,
                               fiber->id, "fiber_trampoline:yield");
        fiber->swap_caller = __builtin_return_address(0);
        fiber->swap_site = "fiber_trampoline:yield";
        job_context_swap(&fiber->ctx, g_scheduler_context);
	assert(fiber->magic1==0xf183);
    	assert(fiber->magic2==0x3a7f);
        #if defined(TRACY_ENABLE) && defined(TRACY_FIBERS)
            if (fiber->tracy_name) {
                TracyCFiberEnter(fiber->tracy_name);
            }
        #endif
    }
}

#if defined(__aarch64__) || defined(__arm__) || defined(__x86_64__)
static void job_fiber_trampoline_arm(uintptr_t raw) {
    job_fiber_t *fiber = (job_fiber_t *)raw;
    assert(fiber->magic1==0xf183);
    assert(fiber->magic2==0x3a7f);
    job_fiber_trampoline_body(fiber);
    assert(fiber->magic1==0xf183);
    assert(fiber->magic2==0x3a7f);
}
#else
static void job_fiber_trampoline(uintptr_t low, uintptr_t high) {
    uintptr_t raw = low | (high << 32);
    job_fiber_t *fiber = (job_fiber_t *)raw;
    assert(fiber->magic1==0xf183);
    assert(fiber->magic2==0x3a7f);
    job_fiber_trampoline_body(fiber);
    assert(fiber->magic1==0xf183);
    assert(fiber->magic2==0x3a7f);
}
#endif

job_fiber_t *job_fiber_create(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id){
    return job_fiber_create_named(sys, fn, user, counter, priority, id, NULL);
}

job_fiber_t *job_fiber_create_named(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id,
                            const char* debug_name) {
    
    if (!sys || !fn) {
        return NULL;
    }
    apool_handle_t fiber_handle = apool_alloc(&sys->fiber_ctx_pool);
    if (fiber_handle.index == APOOL_INDEX_INVALID) {
        return NULL;
    }
    job_fiber_t *fiber = (job_fiber_t *)apool_get(&sys->fiber_ctx_pool, fiber_handle);
    fiber->magic1=0xf183; // stack overflow guard
    fiber->handle = fiber_handle;
    fiber->system = sys;
    fiber->fn = fn;
    fiber->user = user;
    fiber->counter = counter;
    fiber->priority = (uint32_t)priority;
    fiber->finished = 0;
    atomic_store_explicit(&fiber->waiting, 0, memory_order_relaxed);
    fiber->next = NULL;
    fiber->id = id;
    fiber->swap_caller = NULL;
    fiber->swap_site = "created";
    fiber->prev_fiber = NULL;
    fiber->last_worker = UINT32_MAX;
    fiber->magic2=0x3a7f; // stack underflow guard
    #ifdef TRACY_ENABLE
    job_fiber_set_tracy_name(fiber, debug_name);
    #endif
    apool_handle_t fiber_stack_handle = apool_alloc(&sys->fiber_stack_pool);
    if (fiber_stack_handle.index == APOOL_INDEX_INVALID) {
        apool_free(&sys->fiber_ctx_pool, fiber_handle);
	return NULL;
    }
    uint8_t *stack = (uint8_t *)apool_get(&sys->fiber_stack_pool, fiber_stack_handle);
    fiber->stack = stack;
    fiber->stack_handle=fiber_stack_handle;

    /* Paint stack canary at the bottom (low address) of the stack. */
    job_stack_canary_paint(stack, sys->fiber_stack_size);

#if defined(__aarch64__) || defined(__arm__) || defined(__x86_64__)
    job_context_init(&fiber->ctx,
                     job_fiber_trampoline_arm,
                     (uintptr_t)fiber,
                     fiber->stack,
                     sys->fiber_stack_size);
#else
    getcontext(&fiber->ctx);
    fiber->ctx.uc_stack.ss_sp = fiber->stack;
    fiber->ctx.uc_stack.ss_size = sys->fiber_stack_size;
    fiber->ctx.uc_stack.ss_flags = 0;
    fiber->ctx.uc_link = NULL;

    uintptr_t raw = (uintptr_t)fiber;
    uint32_t low = (uint32_t)(raw & 0xFFFFFFFFu);
    uint32_t high = (uint32_t)(raw >> 32);
    makecontext(&fiber->ctx, (void (*)(void))job_fiber_trampoline, 2, (uintptr_t)low, (uintptr_t)high);
#endif
    job_instrument_event("fiber_create", fiber->id, id, g_worker_id, __FILE__, __LINE__);
    return fiber;
}

void job_fiber_destroy(job_fiber_t *fiber) {
    if (!fiber) {
        return;
    }
    fiber->magic1=0xdead; // invalidate
    fiber->magic2=0xdead; 
    fiber->stack = NULL;
    apool_free(&fiber->system->fiber_stack_pool, fiber->stack_handle);
    job_instrument_event("fiber_destroy", fiber->id, 0, g_worker_id, __FILE__, __LINE__);
    /* Wake any workers that might be waiting; ensures teardown makes progress
       even if the last completions race past the broadcast in the trampoline. */
    if (fiber->system) {
        pthread_mutex_lock(&fiber->system->sleep_mtx);
        pthread_cond_broadcast(&fiber->system->sleep_cnd);
        pthread_mutex_unlock(&fiber->system->sleep_mtx);
    }
    apool_free(&fiber->system->fiber_ctx_pool, fiber->handle);
}
