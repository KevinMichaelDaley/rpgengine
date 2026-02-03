#ifndef FERRUM_JOB_WS_DEQUE_H
#define FERRUM_JOB_WS_DEQUE_H

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

/** @file
 * @brief Chase–Lev work-stealing deque.
 *
 * Single owner thread may call push/pop. Multiple thief threads may call steal.
 *
 * This is an internal building block for the job system scheduler.
 */

#ifdef __cplusplus
extern "C" {
#endif

typedef struct fr_ws_deque {
    size_t capacity;
    size_t mask;
    _Atomic size_t top;
    _Atomic size_t bottom;
    void **buffer;
} fr_ws_deque_t;

/**
 * @brief Initialize a deque with a fixed capacity.
 *
 * Capacity is rounded up to the next power of two, minimum 2.
 *
 * @param dq Deque to initialize.
 * @param capacity Desired capacity.
 * @return 0 on success, -1 on invalid args or OOM.
 */
int fr_ws_deque_init(fr_ws_deque_t *dq, size_t capacity);

/**
 * @brief Destroy a deque and free resources.
 * @param dq Deque to destroy (NULL-safe).
 */
void fr_ws_deque_destroy(fr_ws_deque_t *dq);

/**
 * @brief Owner-only push at bottom.
 * @return 0 on success, -1 if full or invalid args.
 */
int fr_ws_deque_push(fr_ws_deque_t *dq, void *item);

/**
 * @brief Owner-only pop from bottom.
 * @return Item pointer or NULL if empty/invalid.
 */
void *fr_ws_deque_pop(fr_ws_deque_t *dq);

/**
 * @brief Thief steal from top.
 * @return Item pointer or NULL if empty/invalid.
 */
void *fr_ws_deque_steal(fr_ws_deque_t *dq);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_JOB_WS_DEQUE_H */
