#ifndef FERRUM_JOB_SPINLOCK_H
#define FERRUM_JOB_SPINLOCK_H

/**
 * @file spinlock.h
 * @brief Fiber-safe spinlock using C11 atomic_flag.
 *
 * Unlike mtx_t / pthread_mutex_t, this spinlock has NO thread-local
 * state and NO ownership tracking.  It is safe to lock on one OS
 * thread and unlock on another — which can happen when a fiber is
 * parked on thread A and resumed on thread B.
 *
 * Intended for short critical sections (< 1 µs) that protect shared
 * data accessed by fiber-based jobs.  Do NOT hold across blocking
 * calls or fiber context-swaps.
 */

#include <stdatomic.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A lightweight spinlock backed by atomic_flag.
 *
 * Initialize with JOB_SPINLOCK_INIT or job_spinlock_init().
 */
typedef struct job_spinlock {
    atomic_flag flag;
} job_spinlock_t;

/** Static initializer. */
#define JOB_SPINLOCK_INIT { .flag = ATOMIC_FLAG_INIT }

/**
 * @brief Initialize a spinlock at runtime.
 * @param lock Non-NULL pointer to the spinlock.
 */
static inline void job_spinlock_init(job_spinlock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_relaxed);
}

/**
 * @brief Acquire the spinlock (busy-wait).
 * @param lock Non-NULL pointer to the spinlock.
 */
static inline void job_spinlock_lock(job_spinlock_t *lock) {
    while (atomic_flag_test_and_set_explicit(&lock->flag,
                                             memory_order_acquire)) {
        /* Spin — on x86 this compiles to a tight loop.
         * For very high contention, a pause intrinsic could be added. */
    }
}

/**
 * @brief Release the spinlock.
 * @param lock Non-NULL pointer to the spinlock.
 */
static inline void job_spinlock_unlock(job_spinlock_t *lock) {
    atomic_flag_clear_explicit(&lock->flag, memory_order_release);
}

/**
 * @brief Destroy a spinlock (no-op, provided for API symmetry).
 * @param lock Non-NULL pointer to the spinlock.
 */
static inline void job_spinlock_destroy(job_spinlock_t *lock) {
    (void)lock;
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_JOB_SPINLOCK_H */
