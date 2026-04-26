/**
 * @file aegis_async.h
 * @brief MPSC async task buffer for script → world subsystem bridging.
 *
 * Scripts enqueue async operations (raycasts, nav queries) into this lock-free
 * ring buffer. The world subsystem drains and executes them without contention.
 *
 * Thread safety:
 * - Multiple producer threads may call submit() concurrently (lock-free CAS).
 * - Exactly one consumer thread calls drain() (single consumer).
 *
 * Ownership:
 * - The buffer owns its slots array (allocated in init, freed in destroy).
 * - result_ptr in each task is NOT owned — it points into the script's
 *   heap arena and is managed by the script fiber.
 */
#ifndef FERRUM_AEGIS_ASYNC_H
#define FERRUM_AEGIS_ASYNC_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stdatomic.h>

/* ------------------------------------------------------------------ */
/* Constants                                                          */
/* ------------------------------------------------------------------ */

/** @brief Async task status values (atomic). */
enum {
    AEGIS_ASYNC_PENDING  = 0, /**< Task is pending execution. */
    AEGIS_ASYNC_COMPLETE = 1, /**< Task completed successfully. */
    AEGIS_ASYNC_ERROR    = 2  /**< Task completed with an error. */
};

/** @brief Async task type identifiers. */
enum {
    AEGIS_TASK_VIS_TEST  = 0, /**< Visibility / raycast test. */
    AEGIS_TASK_NAV_QUERY = 1, /**< Navigation mesh path query. */
    AEGIS_TASK_LLM_PROMPT = 2, /**< LLM prompt (OpenAI-compatible API). */
    AEGIS_TASK_SENSE_QUERY = 3 /**< NPC sense query (proximity + LOS + audio + smell + shadow). */
};

/* ------------------------------------------------------------------ */
/* Task descriptor                                                    */
/* ------------------------------------------------------------------ */

/**
 * @brief An async task submitted by a script fiber.
 *
 * The task carries input parameters (params[64]) and a pointer to a
 * pre-allocated result slot in the script's heap arena. The world
 * subsystem writes result data to result_ptr and atomically sets
 * status to COMPLETE (release ordering). The script polls status
 * with acquire ordering.
 *
 * Nullability:
 * - result_ptr may be NULL if the task produces no output.
 *
 * Side effects: none (pure data).
 */
typedef struct aegis_async_task {
    _Atomic uint32_t status;      /**< AEGIS_ASYNC_PENDING → COMPLETE | ERROR. */
    uint32_t         task_type;   /**< AEGIS_TASK_VIS_TEST, AEGIS_TASK_NAV_QUERY, etc. */
    void            *result_ptr;  /**< Points into script heap arena (fiber-owned). */
    _Atomic uint32_t *status_ptr; /**< Points to VM's tracking entry status (for executor writeback). */
    uint32_t         result_cap;  /**< Pre-allocated result capacity in bytes. */
    uint8_t          params[64];  /**< Task-specific input parameters. */
} aegis_async_task_t;

/* ------------------------------------------------------------------ */
/* Buffer                                                             */
/* ------------------------------------------------------------------ */

/**
 * @brief MPSC lock-free ring buffer for async tasks.
 *
 * Uses atomic CAS on head for multiple producers and a plain tail read
 * for the single consumer. Capacity is rounded up to a power of two.
 * The ring can hold (capacity - 1) entries (one slot is reserved to
 * distinguish full from empty).
 *
 * Each slot has a per-slot committed flag to prevent the consumer from
 * reading partially-written entries (a producer may CAS-claim a slot
 * but not yet have finished writing data).
 *
 * Ownership:
 * - init allocates slots via malloc; destroy frees them.
 * - The caller must not free the buffer while producers may still submit.
 */
typedef struct aegis_async_buffer {
    aegis_async_task_t *slots;      /**< Array of task slots. */
    _Atomic uint32_t   *committed;  /**< Per-slot committed flag (0=empty, 1=written). */
    uint32_t            capacity;   /**< Number of slots (power of 2). */
    uint32_t            mask;       /**< capacity - 1 (fast modulo). */

    /* Cache-line separated to prevent false sharing. */
    _Alignas(64) atomic_uint head; /**< Next write position (producers, CAS). */
    _Alignas(64) atomic_uint tail; /**< Next read position (consumer only). */
} aegis_async_buffer_t;

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

/**
 * @brief Initialize the async task buffer.
 *
 * @param buf      Buffer to initialize (must not be NULL).
 * @param capacity Desired capacity; rounded up to the next power of two.
 *                 The usable capacity is (rounded_capacity - 1).
 * @return true on success, false on allocation failure.
 *
 * Ownership: buf takes ownership of the internal slots array.
 * Error semantics: returns false if malloc fails; buf is zeroed.
 */
bool aegis_async_buffer_init(aegis_async_buffer_t *buf, uint32_t capacity);

/**
 * @brief Destroy the async task buffer and free internal memory.
 *
 * @param buf Buffer to destroy (must not be NULL). Safe to call on a
 *            zeroed buffer.
 *
 * Side effects: frees the slots array.
 */
void aegis_async_buffer_destroy(aegis_async_buffer_t *buf);

/* ------------------------------------------------------------------ */
/* Producer (multiple threads)                                        */
/* ------------------------------------------------------------------ */

/**
 * @brief Submit an async task to the buffer (lock-free, thread-safe).
 *
 * Copies the task into the ring buffer. Multiple threads may call this
 * concurrently. Uses atomic CAS on the head index.
 *
 * @param buf  Buffer (must not be NULL, must be initialized).
 * @param task Task to submit (copied into the ring). Must not be NULL.
 * @return true on success, false if the buffer is full.
 *
 * Error semantics: returns false when full; the task is not enqueued.
 * Side effects: none beyond the atomic head advance.
 */
bool aegis_async_buffer_submit(aegis_async_buffer_t *buf,
                               const aegis_async_task_t *task);

/* ------------------------------------------------------------------ */
/* Consumer (single thread)                                           */
/* ------------------------------------------------------------------ */

/**
 * @brief Drain pending tasks from the buffer (single consumer).
 *
 * Copies up to max_tasks entries into out_tasks and advances the tail.
 * Only one thread may call drain at a time.
 *
 * @param buf       Buffer (must not be NULL, must be initialized).
 * @param out_tasks Output array (must hold at least max_tasks entries).
 * @param max_tasks Maximum number of tasks to drain.
 * @return Number of tasks actually drained (0 if empty).
 *
 * Error semantics: never fails; returns 0 when empty.
 * Side effects: advances the tail index.
 */
uint32_t aegis_async_buffer_drain(aegis_async_buffer_t *buf,
                                  aegis_async_task_t *out_tasks,
                                  uint32_t max_tasks);

/**
 * @brief Return the number of pending tasks in the buffer.
 *
 * @param buf Buffer (must not be NULL, must be initialized).
 * @return Number of tasks currently in the ring.
 *
 * Note: This is a snapshot; the value may change concurrently.
 */
uint32_t aegis_async_buffer_count(const aegis_async_buffer_t *buf);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_AEGIS_ASYNC_H */
