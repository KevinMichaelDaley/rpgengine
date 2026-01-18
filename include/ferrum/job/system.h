#ifndef FERRUM_JOB_SYSTEM_H
#define FERRUM_JOB_SYSTEM_H

#include <stddef.h>
#include <stdint.h>

/** @file
 * @brief Public API for the job system and fibers.
 */

#ifdef __cplusplus
extern "C" {
#endif

struct job_counter;

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
job_system_t *job_system_create(uint32_t worker_count,
                                uint32_t queue_capacity,
                                size_t fiber_stack_size,
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

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_JOB_SYSTEM_H */
