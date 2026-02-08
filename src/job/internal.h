#ifndef FERRUM_JOB_INTERNAL_H
#define FERRUM_JOB_INTERNAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <pthread.h>

#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/job/spinlock.h"
#include "ferrum/job/instrumentation.h"
#include "ferrum/job/stack_canary.h"

#include "ferrum/memory/arena.h"
#include "ferrum/memory/apool.h"
#include "context.h"
#ifdef TRACY_ENABLE
#include "tracy/TracyC.h"
#endif
#define JOB_MIN_STACK (4u * 1024u)

typedef struct job_fiber job_fiber_t;

struct job_entry {
    job_fiber_t *fiber;
    int priority;
    uint64_t id;
};

struct job_fiber {
    uint16_t magic1;
    #ifdef TRACY_ENABLE
    const char* tracy_name;
    char tracy_name_storage[64];
    TracyCZoneCtx zone;
    #endif
    job_context_t ctx;
    apool_handle_t handle;
    apool_handle_t stack_handle;
    job_system_t *system;
    void (*fn)(void *);
    void *user;
    struct job_counter *counter;
    int finished;

    /* Wait/wake states (atomic to prevent double-enqueue race).
     *   0 = not waiting (running or finished)
     *   1 = parked (fiber has yielded, waiting for counter)
     *   2 = woken-before-park-observed (counter hit 0 before run_entry saw waiting=1)
     * Only run_entry re-enqueues a woken fiber, ensuring the fiber has actually
     * yielded before it can be scheduled on another worker. */
    atomic_int waiting;
    uint32_t priority;
    struct job_fiber *next;
    uint64_t id;
    uint8_t* stack;

    /* Debug trace: saved at each context swap for post-mortem diagnosis.
     * swap_caller:  __builtin_return_address(0) of the function that swapped.
     * swap_site:    human-readable label of the swap point.
     * prev_fiber:   the fiber that was running on this worker immediately before
     *               this fiber was scheduled (set in run_entry).
     * last_worker:  worker id that last ran this fiber. */
    void *swap_caller;
    const char *swap_site;
    struct job_fiber *prev_fiber;
    uint32_t last_worker;

    uint16_t magic2;
};

extern _Thread_local job_fiber_t *g_current_fiber;
extern _Thread_local job_system_t *g_current_system;
extern _Thread_local job_context_t *g_scheduler_context;
extern _Thread_local uint32_t g_worker_id;
extern _Thread_local uint32_t g_worker_node;

void job_system_wake_waiters(job_system_t *sys, job_counter_t *counter);
void job_system_wake_waiters_locked(job_system_t *sys, job_counter_t *counter);
int job_system_enqueue(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id);
int job_system_enqueue_preferred(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id, uint32_t preferred_worker);
int job_system_pop_next(job_system_t *sys, struct job_entry *out_entry);
void job_fiber_destroy(job_fiber_t *fiber);
void run_entry(job_system_t *sys, const struct job_entry *entry, job_context_t *sched_ctx);
job_fiber_t *job_fiber_create(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id);
job_fiber_t *job_fiber_create_named(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id,
                              const char* debug_name
                            );

#endif /* FERRUM_JOB_INTERNAL_H */
