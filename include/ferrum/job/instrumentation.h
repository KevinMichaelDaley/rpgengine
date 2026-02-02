#ifndef FERRUM_JOB_INSTRUMENTATION_H
#define FERRUM_JOB_INSTRUMENTATION_H

#include <stdint.h>
#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @file
 * @brief Public API for job system instrumentation.
 *
 * Instrumentation is compiled-in and can be toggled at runtime.
 * Define the macro JOB_SYS_INSTRUMENT_DEFAULT_ON to enable by default.
 */

/**
 * @brief Enable or disable instrumentation at runtime.
 *
 * @param on Non-zero to enable, 0 to disable.
 */
void job_instrument_enable(int on);

/**
 * @brief Return the number of recorded instrumentation events.
 *
 * @return Count of events since start.
 */
uint64_t job_instrument_count(void);

/**
 * @brief Record an instrumentation event.
 *
 * This is called by the job system when instrumentation is enabled.
 * Users generally don't need to call this directly.
 *
 * @param event A short event string (e.g., "enqueue", "pop", "start", "complete", "magic_invalid").
 * @param fiber_id The fiber identifier, or 0 if none.
 * @param job_id The job identifier.
 * @param worker_id The executing worker id (or UINT32_MAX if not inside a job).
 * @param file Source file of the event.
 * @param line Source line of the event.
 */
void job_instrument_event(const char *event,
                          uint64_t fiber_id,
                          uint64_t job_id,
                          uint32_t worker_id,
                          const char *file,
                          int line);

/**
 * @brief Initialize instrumentation (default-on or via env).
 *
 * Checks env var `JOB_SYS_INSTR` ("1" enables, "0" disables). If unset,
 * uses compile-time default (enabled when JOB_SYS_INSTRUMENT_DEFAULT_ON is defined).
 */
void job_instrument_init(void);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_JOB_INSTRUMENTATION_H */
