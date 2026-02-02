#include <limits.h>
#include <stdint.h>
#include <stdlib.h>

#include "internal.h"

_Thread_local job_fiber_t *g_current_fiber = NULL;
_Thread_local job_system_t *g_current_system = NULL;
_Thread_local job_context_t *g_scheduler_context = NULL;
_Thread_local uint32_t g_worker_id = UINT32_MAX;

static void job_fiber_trampoline_body(job_fiber_t *fiber) {
    g_current_fiber = fiber;
    g_current_system = fiber->system;

    fiber->fn(fiber->user);
    fiber->finished = 1;

    if (fiber->counter) {
        job_counter_dec(fiber->counter);
    }
    atomic_fetch_add_explicit(&fiber->system->jobs_completed, 1, memory_order_release);

    cnd_broadcast(&fiber->system->queue_cond);

    for (;;) {
        job_context_swap(&fiber->ctx, g_scheduler_context);
    }
}

#if defined(__aarch64__) || defined(__arm__) || defined(__x86_64__)
static void job_fiber_trampoline_arm(uintptr_t raw) {
    job_fiber_t *fiber = (job_fiber_t *)raw;
    job_fiber_trampoline_body(fiber);
}
#else
static void job_fiber_trampoline(uintptr_t low, uintptr_t high) {
    uintptr_t raw = low | (high << 32);
    job_fiber_t *fiber = (job_fiber_t *)raw;
    job_fiber_trampoline_body(fiber);
}
#endif

job_fiber_t *job_fiber_create(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id) {
    if (!sys || !fn) {
        return NULL;
    }
    apool_handle_t fiber_handle = apool_alloc(&sys->fiber_stack_pool);
    if (fiber_handle.index == APOOL_INDEX_INVALID) {
        return NULL;
    }
    job_fiber_t *fiber = (job_fiber_t *)apool_get(&sys->fiber_stack_pool, fiber_handle);
    fiber->magic1=0xf183; // stack overflow guard
    fiber->handle = fiber_handle;
    fiber->system = sys;
    fiber->fn = fn;
    fiber->user = user;
    fiber->counter = counter;
    fiber->priority = (uint32_t)priority;
    fiber->finished = 0;
    fiber->waiting = 0;
    fiber->next = NULL;
    fiber->id = id;
    fiber->magic2=0x3a7f; // stack underflow guard
    /* Stack starts immediately after the fiber struct in the pool block. */
    fiber->stack = (uint8_t *)fiber + sizeof(job_fiber_t);
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
    job_instrument_event("fiber_destroy", fiber->id, 0, g_worker_id, __FILE__, __LINE__);
    /* Wake any workers that might be waiting; ensures teardown makes progress
       even if the last completions race past the broadcast in the trampoline. */
    if (fiber->system) {
        cnd_broadcast(&fiber->system->queue_cond);
    }
    apool_free(&fiber->system->fiber_stack_pool, fiber->handle);
}
