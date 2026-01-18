#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "internal.h"

struct worker_arg {
    job_system_t *sys;
    uint32_t id;
};

static int worker_main(void *arg);
void run_entry(job_system_t *sys, const struct job_entry *entry, ucontext_t *sched_ctx);
static void cleanup_system(job_system_t *sys);

job_system_t *job_system_create(uint32_t worker_count,
                                uint32_t queue_capacity,
                                size_t fiber_stack_size,
                                int deterministic_mode) {
    if (fiber_stack_size < JOB_MIN_STACK || queue_capacity == 0) {
        return NULL;
    }
    if (!deterministic_mode && worker_count == 0) {
        return NULL;
    }

    job_system_t *sys = (job_system_t *)calloc(1, sizeof(job_system_t));
    if (!sys) {
        return NULL;
    }

    sys->worker_count = deterministic_mode ? 1u : worker_count;
    sys->queue_capacity = queue_capacity;
    sys->fiber_stack_size = fiber_stack_size;
    sys->deterministic = deterministic_mode ? 1 : 0;
    atomic_init(&sys->running, false);
    atomic_init(&sys->shutting_down, false);
    atomic_init(&sys->next_job_id, 1);
    atomic_init(&sys->jobs_started, 0);
    atomic_init(&sys->jobs_completed, 0);

    if (mtx_init(&sys->queue_lock, mtx_plain) != thrd_success) {
        free(sys);
        return NULL;
    }
    if (cnd_init(&sys->queue_cond) != thrd_success) {
        mtx_destroy(&sys->queue_lock);
        free(sys);
        return NULL;
    }

    sys->queue = (struct job_entry *)calloc(queue_capacity, sizeof(struct job_entry));
    sys->workers = (thrd_t *)calloc(sys->worker_count, sizeof(thrd_t));
    if (!sys->queue || !sys->workers) {
        cleanup_system(sys);
        return NULL;
    }

    return sys;
}

int job_system_start(job_system_t *sys) {
    if (!sys) {
        return -1;
    }
    if (atomic_exchange(&sys->running, true)) {
        return 0;
    }

    if (sys->deterministic) {
        return 0;
    }

    for (uint32_t i = 0; i < sys->worker_count; ++i) {
        struct worker_arg *wa = (struct worker_arg *)malloc(sizeof(struct worker_arg));
        if (!wa) {
            atomic_store(&sys->shutting_down, true);
            cnd_broadcast(&sys->queue_cond);
            for (uint32_t j = 0; j < i; ++j) {
                thrd_join(sys->workers[j], NULL);
            }
            return -1;
        }
        wa->sys = sys;
        wa->id = i;
        if (thrd_create(&sys->workers[i], worker_main, wa) != thrd_success) {
            atomic_store(&sys->shutting_down, true);
            cnd_broadcast(&sys->queue_cond);
            free(wa);
            for (uint32_t j = 0; j < i; ++j) {
                thrd_join(sys->workers[j], NULL);
            }
            return -1;
        }
    }

    return 0;
}

int job_system_wait_idle(job_system_t *sys) {
    if (!sys) {
        return -1;
    }

    if (sys->deterministic) {
        ucontext_t sched_ctx;
        g_scheduler_context = &sched_ctx;
        g_worker_id = 0;
        g_current_system = sys;

        struct job_entry entry;
        while (job_system_pop_next(sys, &entry) == 0) {
            run_entry(sys, &entry, &sched_ctx);
        }
        return 0;
    }

    mtx_lock(&sys->queue_lock);
    for (;;) {
        uint64_t started = atomic_load(&sys->jobs_started);
        uint64_t completed = atomic_load(&sys->jobs_completed);
        if (sys->queue_size == 0 && started == completed) {
            break;
        }
        if (atomic_load(&sys->shutting_down) && sys->queue_size == 0) {
            break;
        }
        cnd_wait(&sys->queue_cond, &sys->queue_lock);
    }
    mtx_unlock(&sys->queue_lock);
    return 0;
}

void job_system_shutdown(job_system_t *sys) {
    if (!sys) {
        return;
    }

    atomic_store(&sys->shutting_down, true);
    cnd_broadcast(&sys->queue_cond);

    if (sys->deterministic) {
        job_system_wait_idle(sys);
        cleanup_system(sys);
        return;
    }

    for (uint32_t i = 0; i < sys->worker_count; ++i) {
        thrd_join(sys->workers[i], NULL);
    }

    cleanup_system(sys);
}

static void cleanup_system(job_system_t *sys) {
    if (!sys) {
        return;
    }
    cnd_destroy(&sys->queue_cond);
    mtx_destroy(&sys->queue_lock);
    free(sys->queue);
    free(sys->workers);
    free(sys);
}

void run_entry(job_system_t *sys, const struct job_entry *entry, ucontext_t *sched_ctx) {
    g_current_fiber = entry->fiber;
    g_current_system = sys;
    g_scheduler_context = sched_ctx;

    swapcontext(sched_ctx, &entry->fiber->ctx);

    if (entry->fiber->finished) {
        job_fiber_destroy(entry->fiber);
    } else if (!entry->fiber->waiting) {
        if (job_system_enqueue(sys, entry->fiber, entry->priority, entry->id) != 0) {
            job_fiber_destroy(entry->fiber);
        }
    }

    g_current_fiber = NULL;
}

static int worker_main(void *arg) {
    struct worker_arg *wa = (struct worker_arg *)arg;
    job_system_t *sys = wa->sys;
    g_worker_id = wa->id;
    g_current_system = sys;

    ucontext_t sched_ctx;
    g_scheduler_context = &sched_ctx;

    struct job_entry entry;
    for (;;) {
        if (job_system_pop_next(sys, &entry) != 0) {
            if (atomic_load(&sys->shutting_down)) {
                break;
            }
            continue;
        }
        run_entry(sys, &entry, &sched_ctx);
    }

    free(wa);
    return 0;
}
