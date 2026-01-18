#ifndef FERRUM_JOB_COUNTER_H
#define FERRUM_JOB_COUNTER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Opaque waitable counter. */
typedef struct job_counter job_counter_t;

/** Status results for wait operations. */
typedef enum job_wait_status {
    JOB_WAIT_OK = 0,
    JOB_WAIT_TIMEOUT = 1,
    JOB_WAIT_INVALID = 2
} job_wait_status_t;

/**
 * @brief Initialize a counter with an initial value.
 * @param counter Counter pointer (non-NULL).
 * @param initial Initial count value.
 */
void job_counter_init(job_counter_t *counter, uint32_t initial);

/**
 * @brief Increase counter by a positive amount.
 * @return 0 on success, -1 on invalid input or overflow attempt.
 */
int job_counter_add(job_counter_t *counter, uint32_t value);

/**
 * @brief Decrement counter by one.
 * @return 0 on success, -1 on underflow or invalid input.
 */
int job_counter_dec(job_counter_t *counter);

/**
 * @brief Current counter value snapshot.
 */
uint32_t job_counter_value(const job_counter_t *counter);

/**
 * @brief Wait for counter to reach zero. Parks the current fiber; does not block OS thread.
 * @param counter Counter pointer.
 * @param spin_count Optional spin iterations before parking (0 for immediate park).
 * @return job_wait_status_t status.
 */
job_wait_status_t job_wait_counter(job_counter_t *counter, uint32_t spin_count);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_JOB_COUNTER_H */
