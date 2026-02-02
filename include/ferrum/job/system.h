#ifndef FERRUM_JOB_SYSTEM_H
#define FERRUM_JOB_SYSTEM_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>
#include "counter.h"
#include "ferrum/memory/pool.h"
/** @file
 * @brief Public API for the job system and fibers.
 */

#ifdef __cplusplus
extern "C" {
#endif
struct job_system {
    uint32_t worker_count;
    uint32_t queue_capacity;
    size_t fiber_stack_size;
    pool_t fiber_stack_pool;
    int deterministic;
    atomic_bool running;
    atomic_bool shutting_down;
    struct job_entry *queue;
    atomic_int *queue_slot_state; /* 0=empty, 1=ready, 2=busy */
    atomic_uint queue_insert_cursor;
    atomic_uint queue_pop_cursor;
    mtx_t queue_lock;
    cnd_t queue_cond;

    thrd_t *workers;
    atomic_uint_least64_t next_job_id;
    atomic_uint_least64_t jobs_started;
    atomic_uint_least64_t jobs_completed;
};

struct job_counter;

typedef enum job_system_create_status{
    JOB_CREATE_OK = 0,
    JOB_CREATE_ERR_INVALID,
    JOB_CREATE_ERR_OOM,
    JOB_CREATE_ERR_MTX_INIT,
    JOB_CREATE_ERR_CND_INIT,
    JOB_CREATE_POOL_INIT_ERR
} job_system_create_status_t;
/** Opaque handle to the job system. */
typedef struct job_system job_system_t;

/** Unique identifier for a dispatched job. */
typedef uint64_t job_id_t;

/** Invalid job identifier sentinel. */
#define JOB_ID_INVALID ((job_id_t)0)

/**
 * @brief Create a job system instance.
 *
 * @param worker_count Number of worker threads to spawn. Must be >= 1 unless deterministic mode.
 * @param queue_capacity Maximum number of runnable jobs that can be queued.
 * @param fiber_stack_size Stack size in bytes for each fiber. Must be sufficiently large (>= 16384).
 * @param deterministic_mode Non-zero enables single-threaded deterministic scheduling.
 * @return job_system_t* New instance or NULL on validation/allocation failure.
 */
job_system_create_status_t job_system_create(job_system_t* sys,
                                uint32_t worker_count,
                                uint32_t queue_capacity,
                                size_t fiber_stack_size,
                                size_t fiber_count_max,
                                int deterministic_mode);

/**
 * @brief Start worker threads (or single-thread loop when deterministic).
 *
 * @param sys Job system pointer.
 * @return 0 on success, -1 on invalid input or thread startup failure.
 */
int job_system_start(job_system_t *sys);

/**
 * @brief Dispatch a job onto the system.
 *
 * @param sys Job system pointer.
 * @param fn Function to execute on a fiber.
 * @param user_data User pointer passed to the job.
 * @param priority Signed priority hint; higher values run first when contended.
 * @param counter Optional counter to increment before dispatch and auto-decrement on completion.
 * @return job_id_t Valid identifier or JOB_ID_INVALID on error (invalid args, full queue, or shutdown).
 */
job_id_t job_dispatch(job_system_t *sys,
                     void (*fn)(void *user_data),
                     void *user_data,
                     int priority,
                     struct job_counter *counter);

/**
 * @brief Wait until all runnable jobs complete and all counters reach zero.
 *
 * @param sys Job system pointer.
 * @return 0 on success; -1 on invalid input.
 */
int job_system_wait_idle(job_system_t *sys);

/**
 * @brief Request graceful shutdown and join workers. Blocks until completion.
 *
 * @param sys Job system pointer. NULL is ignored.
 */
void job_system_shutdown(job_system_t *sys);

/**
 * @brief Cooperatively yield the current fiber back to the scheduler.
 * Must be called from a running job.
 */
void job_yield(void);

/**
 * @brief Obtain the current worker identifier.
 *
 * @return Worker index (zero-based) or UINT32_MAX when not inside a job.
 */
uint32_t job_current_worker_id(void);

/**
 * @brief Instrumentation: number of jobs started.
 */
uint64_t job_system_jobs_started(const job_system_t *sys);

/**
 * @brief Instrumentation: number of jobs completed.
 */
uint64_t job_system_jobs_completed(const job_system_t *sys);

/**
 * @brief Indicates whether the job queue implementation uses lock-free operations.
 *
 * This returns 1 when enqueue/dequeue avoid mutex-based critical sections and
 * use atomic operations suitable for MPMC usage; 0 otherwise.
 */
int job_system_queue_is_lock_free(const job_system_t *sys);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_JOB_SYSTEM_H */
