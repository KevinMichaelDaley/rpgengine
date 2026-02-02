#ifndef FERRUM_JOB_INTERNAL_H
#define FERRUM_JOB_INTERNAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <threads.h>

#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/job/instrumentation.h"

#include "ferrum/memory/arena.h"
#include "ferrum/memory/apool.h"
#include "context.h"

#define JOB_MIN_STACK (4u * 1024u)

typedef struct job_fiber job_fiber_t;

struct job_entry {
    job_fiber_t *fiber;
    int priority;
    uint64_t id;
};

struct job_fiber {
    uint16_t magic1;
    job_context_t ctx;
    apool_handle_t handle;
    job_system_t *system;
    void (*fn)(void *);
    void *user;
    struct job_counter *counter;
    int finished;
    int waiting;
    uint32_t priority;
    struct job_fiber *next;
    uint64_t id;
    uint8_t* stack;
    uint16_t magic2;
};

extern _Thread_local job_fiber_t *g_current_fiber;
extern _Thread_local job_system_t *g_current_system;
extern _Thread_local job_context_t *g_scheduler_context;
extern _Thread_local uint32_t g_worker_id;

void job_system_wake_waiters(job_system_t *sys, job_counter_t *counter);
int job_system_enqueue(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id);
int job_system_pop_next(job_system_t *sys, struct job_entry *out_entry);
void job_fiber_destroy(job_fiber_t *fiber);
void run_entry(job_system_t *sys, const struct job_entry *entry, job_context_t *sched_ctx);
job_fiber_t *job_fiber_create(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id);

#endif /* FERRUM_JOB_INTERNAL_H */
