#ifndef FERRUM_JOB_INTERNAL_H
#define FERRUM_JOB_INTERNAL_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <threads.h>
#include <ucontext.h>

#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"

#define JOB_MIN_STACK (16u * 1024u)

typedef struct job_fiber job_fiber_t;

struct job_entry {
    job_fiber_t *fiber;
    int priority;
    uint64_t id;
};

struct job_system {
    uint32_t worker_count;
    uint32_t queue_capacity;
    size_t fiber_stack_size;
    int deterministic;
    atomic_bool running;
    atomic_bool shutting_down;

    mtx_t queue_lock;
    cnd_t queue_cond;
    struct job_entry *queue;
    uint32_t queue_size;
    uint32_t queue_head;
    uint32_t queue_tail;

    thrd_t *workers;
    atomic_uint_least64_t next_job_id;
    atomic_uint_least64_t jobs_started;
    atomic_uint_least64_t jobs_completed;
};

struct job_fiber {
    ucontext_t ctx;
    job_system_t *system;
    void (*fn)(void *);
    void *user;
    struct job_counter *counter;
    uint8_t *stack;
    int finished;
    int waiting;
    uint32_t priority;
    struct job_fiber *next;
};

extern _Thread_local job_fiber_t *g_current_fiber;
extern _Thread_local job_system_t *g_current_system;
extern _Thread_local ucontext_t *g_scheduler_context;
extern _Thread_local uint32_t g_worker_id;

void job_system_wake_waiters(job_system_t *sys, job_counter_t *counter);
int job_system_enqueue(job_system_t *sys, job_fiber_t *fiber, int priority, uint64_t id);
int job_system_pop_next(job_system_t *sys, struct job_entry *out_entry);
void job_fiber_destroy(job_fiber_t *fiber);
void run_entry(job_system_t *sys, const struct job_entry *entry, ucontext_t *sched_ctx);
job_fiber_t *job_fiber_create(job_system_t *sys,
                              void (*fn)(void *),
                              void *user,
                              job_counter_t *counter,
                              int priority,
                              uint64_t id);

#endif /* FERRUM_JOB_INTERNAL_H */
