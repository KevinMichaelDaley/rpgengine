#define _GNU_SOURCE
#include <stdatomic.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#ifdef __linux__
#include <pthread.h>
#include <sched.h>
#include <unistd.h>
#endif

#include "internal.h"

struct worker_arg {
    job_system_t *sys;
    uint32_t id;
};
static int worker_main(void *arg);
void run_entry(job_system_t *sys, const struct job_entry *entry, job_context_t *sched_ctx);
static void cleanup_system(job_system_t *sys);
static int pop_next_sharded(job_system_t *sys, struct job_entry *out_entry, uint32_t preferred);

job_system_create_status_t job_system_create(job_system_t* sys,
                                uint32_t worker_count,
                                uint32_t queue_capacity,
                                size_t fiber_stack_size,
                                size_t fiber_count_max,
                                int deterministic_mode) {
    if (fiber_stack_size < JOB_MIN_STACK || queue_capacity == 0) {
        return JOB_CREATE_ERR_INVALID;
    }
    if (!deterministic_mode && worker_count == 0) {
        return JOB_CREATE_ERR_INVALID;
    }
    if (!sys) {
        return JOB_CREATE_ERR_INVALID;
    }

    sys->worker_count = deterministic_mode ? 1u : worker_count;
    sys->queue_capacity = queue_capacity;
    sys->fiber_stack_size = fiber_stack_size;
    apool_status_t pool_status = apool_init(&sys->fiber_stack_pool, fiber_count_max, sizeof(job_fiber_t) + fiber_stack_size);
    if( pool_status != APOOL_OK) {
        if(pool_status == APOOL_ERR_INVALID) {
            return JOB_CREATE_POOL_INIT_ERR;
        } else {
            return JOB_CREATE_ERR_OOM;
        }
    }
    sys->deterministic = deterministic_mode ? 1 : 0;
    atomic_init(&sys->running, false);
    atomic_init(&sys->shutting_down, false);
    atomic_init(&sys->next_job_id, 1);
    atomic_init(&sys->jobs_started, 0);
    atomic_init(&sys->jobs_completed, 0);
    sys->queue = (struct job_entry *)calloc(queue_capacity, sizeof(struct job_entry));
    sys->queue_slot_state = (atomic_int *)calloc(queue_capacity, sizeof(atomic_int));
    sys->workers = (thrd_t *)calloc(sys->worker_count, sizeof(thrd_t));
    if (!sys->queue || !sys->queue_slot_state || !sys->workers) {
        cleanup_system(sys);
        return JOB_CREATE_ERR_OOM;
    }

    for (uint32_t i = 0; i < sys->queue_capacity; ++i) {
        atomic_init(&sys->queue_slot_state[i], 0);
    }
    atomic_init(&sys->queue_insert_cursor, 0);
    atomic_init(&sys->queue_pop_cursor, 0);

    if (mtx_init(&sys->queue_lock, mtx_plain) != thrd_success) {
        return JOB_CREATE_ERR_MTX_INIT;
    }
    if (cnd_init(&sys->queue_cond) != thrd_success) {
        mtx_destroy(&sys->queue_lock);
        return JOB_CREATE_ERR_CND_INIT;
    }

    /* Initialize flags and instrumentation */
    atomic_init(&sys->affinity_enabled, false);
    sys->numa_enabled = 0;
    sys->numa_node_count = 1u;
    job_instrument_init();

    return JOB_CREATE_OK;
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
        /* Optionally set CPU affinity on Linux */
#ifdef __linux__
        if (atomic_load(&sys->affinity_enabled)) {
            cpu_set_t set;
            CPU_ZERO(&set);
            long nproc = sysconf(_SC_NPROCESSORS_ONLN);
            if (nproc < 1) nproc = 1;
            int cpu = (int)(i % (uint32_t)nproc);
            CPU_SET(cpu, &set);
            pthread_setaffinity_np((pthread_t)sys->workers[i], sizeof(set), &set);
        }
#endif
    }

    return 0;
}

int job_system_wait_idle(job_system_t *sys) {
    if (!sys) {
        return -1;
    }

    if (sys->deterministic) {
        job_context_t sched_ctx;
        g_scheduler_context = &sched_ctx;
        g_worker_id = 0;
        g_current_system = sys;

        struct job_entry entry;
        while (job_system_pop_next(sys, &entry) == 0) {
            run_entry(sys, &entry, &sched_ctx);
        }
        return 0;
    }

    for (;;) {
        uint64_t started = atomic_load_explicit(&sys->jobs_started, memory_order_acquire);
        uint64_t completed = atomic_load_explicit(&sys->jobs_completed, memory_order_acquire);
        int any_ready = 0;
        for (uint32_t i = 0; i < sys->queue_capacity; ++i) {
            if (atomic_load_explicit(&sys->queue_slot_state[i], memory_order_acquire) == 1) {
                any_ready = 1;
                break;
            }
        }
        if (!any_ready && started == completed) {
            break;
        }
        if (atomic_load(&sys->shutting_down) && !any_ready) {
            break;
        }
        thrd_yield();
    }
    return 0;
}

void job_system_shutdown(job_system_t *sys) {
    if (!sys) {
        return;
    }

    /* Stop accepting new work immediately. */
    atomic_store(&sys->running, false);
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
    free(sys->queue_slot_state);
    free(sys->workers);
    apool_destroy(&sys->fiber_stack_pool);
}
static int pop_next_sharded(job_system_t *sys, struct job_entry *out_entry, uint32_t preferred) {
    /* Prefer local shard by scanning READY slots near a per-worker cursor; fall back to steals. */
    uint32_t start = preferred % sys->queue_capacity;
    for (uint32_t i = 0; i < sys->queue_capacity; ++i) {
        uint32_t idx = (start + i) % sys->queue_capacity;
        int state = atomic_load_explicit(&sys->queue_slot_state[idx], memory_order_acquire);
        if (state == 1) {
            int expected = 1;
            if (atomic_compare_exchange_strong(&sys->queue_slot_state[idx], &expected, 2)) {
                *out_entry = sys->queue[idx];
                atomic_store_explicit(&sys->queue_slot_state[idx], 0, memory_order_release);
                return 0;
            }
        }
    }
    return -1;
}

void run_entry(job_system_t *sys, const struct job_entry *entry, job_context_t *sched_ctx) {
    g_current_fiber = entry->fiber;
    g_current_system = sys;
    g_scheduler_context = sched_ctx;
    if (entry->fiber->magic1 != 0xf183 || entry->fiber->magic2 != 0x3a7f) {
        job_instrument_event("magic_invalid", entry->fiber->id, entry->id, g_worker_id, __FILE__, __LINE__);
    }
    job_instrument_event("start", entry->fiber->id, entry->id, g_worker_id, __FILE__, __LINE__);
    job_context_swap(sched_ctx, &entry->fiber->ctx);

    if (entry->fiber->finished) {
        job_instrument_event("complete", entry->fiber->id, entry->id, g_worker_id, __FILE__, __LINE__);
        job_fiber_destroy(entry->fiber);
    } else if (!entry->fiber->waiting) {
        if (job_system_enqueue(sys, entry->fiber, entry->priority, entry->id) != 0) {
            job_fiber_destroy(entry->fiber);
        } else {
            job_instrument_event("continue", entry->fiber->id, entry->id, g_worker_id, __FILE__, __LINE__);
        }
    }

    g_current_fiber = NULL;
}

static int worker_main(void *arg) {
    struct worker_arg *wa = (struct worker_arg *)arg;
    job_system_t *sys = wa->sys;
    g_worker_id = wa->id;
    g_worker_node = sys->numa_enabled ? (g_worker_id % (sys->numa_node_count ? sys->numa_node_count : 1u)) : 0u;
    g_current_system = sys;

    job_context_t sched_ctx;
    g_scheduler_context = &sched_ctx;

    struct job_entry entry;
    for (;;) {
        if (pop_next_sharded(sys, &entry, g_worker_id) != 0) {
            if (atomic_load(&sys->shutting_down)) {
                break;
            }
            /* Sleep when no READY entries to avoid wasted spinning. */
            mtx_lock(&sys->queue_lock);
            for (;;) {
                int any_ready = 0;
                for (uint32_t i = 0; i < sys->queue_capacity; ++i) {
                    if (atomic_load_explicit(&sys->queue_slot_state[i], memory_order_acquire) == 1) {
                        any_ready = 1;
                        break;
                    }
                }
                if (atomic_load(&sys->shutting_down) || any_ready) {
                    break;
                }
                cnd_wait(&sys->queue_cond, &sys->queue_lock);
            }
            mtx_unlock(&sys->queue_lock);
            continue;
        }
        run_entry(sys, &entry, &sched_ctx);
    }

    free(wa);
    return 0;
}

int job_system_queue_is_lock_free(const job_system_t *sys) {
    (void)sys;
    return 1;
}

int job_system_queue_is_sharded(const job_system_t *sys) {
    (void)sys;
    return 1;
}

int job_system_enable_affinity(job_system_t *sys, int enable) {
    if (!sys) return -1;
    atomic_store(&sys->affinity_enabled, (enable != 0));
    return 0;
}

int job_system_affinity_enabled(const job_system_t *sys) {
    if (!sys) return 0;
    return atomic_load(&sys->affinity_enabled) ? 1 : 0;
}

job_id_t job_dispatch_to(job_system_t *sys,
                         void (*fn)(void *user_data),
                         void *user_data,
                         int priority,
                         struct job_counter *counter,
                         uint32_t preferred_worker) {
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
    atomic_fetch_add_explicit(&sys->jobs_started, 1, memory_order_release);
    /* Insert into preferred region first; fall back to global scan. */
    uint32_t start = (preferred_worker == UINT32_MAX) ? atomic_load(&sys->queue_insert_cursor) : preferred_worker % sys->queue_capacity;
    for (uint32_t i = 0; i < sys->queue_capacity; ++i) {
        uint32_t idx = (start + i) % sys->queue_capacity;
        int expected = 0;
        if (atomic_compare_exchange_strong(&sys->queue_slot_state[idx], &expected, 2)) {
            sys->queue[idx].fiber = fiber;
            sys->queue[idx].priority = priority;
            sys->queue[idx].id = id;
            atomic_store_explicit(&sys->queue_slot_state[idx], 1, memory_order_release);
            cnd_broadcast(&sys->queue_cond);
            job_instrument_event("enqueue", fiber->id, id, g_worker_id, __FILE__, __LINE__);
            return id;
        }
    }
    job_fiber_destroy(fiber);
    if (counter) {
        job_counter_dec(counter);
    }
    return JOB_ID_INVALID;
}
