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
    atomic_fetch_add_explicit(&fiber->system->jobs_completed, 1, memory_order_relaxed);

    cnd_broadcast(&fiber->system->queue_cond);

    job_context_swap(&fiber->ctx, g_scheduler_context);
}

#if defined(__aarch64__) || defined(__arm__)
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

    job_fiber_t *fiber = (job_fiber_t *)calloc(1, sizeof(job_fiber_t));
    if (!fiber) {
        return NULL;
    }

    fiber->stack = (uint8_t *)malloc(sys->fiber_stack_size);
    if (!fiber->stack) {
        free(fiber);
        return NULL;
    }

    fiber->system = sys;
    fiber->fn = fn;
    fiber->user = user;
    fiber->counter = counter;
    fiber->priority = (uint32_t)priority;
    fiber->finished = 0;
    fiber->waiting = 0;
    fiber->next = NULL;

#if defined(__aarch64__) || defined(__arm__)
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

    (void)id; /* reserved for future tracing */
    return fiber;
}

void job_fiber_destroy(job_fiber_t *fiber) {
    if (!fiber) {
        return;
    }
    free(fiber->stack);
    free(fiber);
}
