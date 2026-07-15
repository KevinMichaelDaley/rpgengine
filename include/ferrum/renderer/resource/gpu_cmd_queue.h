/**
 * @file gpu_cmd_queue.h
 * @brief Bounded lock-free MPSC ring of @ref gpu_cmd_t: many loader fibers push,
 *        the single render/main thread pops and executes.
 *
 * Storage is caller-provided (no dynamic allocation): `capacity` command slots
 * plus `capacity` per-slot state words. `capacity` need not be a power of two.
 *
 * Concurrency: @ref gpu_cmd_push is safe from any number of producer threads;
 * @ref gpu_cmd_pop must be called from exactly ONE consumer thread. Producers
 * reserve a monotonic ticket (CAS on the tail), publish into their slot, and
 * flag it ready; the consumer reads the head slot once its flag is ready, then
 * clears it. FIFO across the whole queue, and per-producer FIFO always holds.
 *
 * Ownership: the queue borrows the slot + state storage; it owns neither those
 * nor the @ref gpu_cmd_t::data payloads.
 */
#ifndef FERRUM_RENDERER_RESOURCE_GPU_CMD_QUEUE_H
#define FERRUM_RENDERER_RESOURCE_GPU_CMD_QUEUE_H

#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/resource/gpu_cmd.h"

#ifdef __cplusplus
extern "C" {
#endif

/** MPSC ring over caller-provided slot + state arrays. */
typedef struct gpu_cmd_queue {
    gpu_cmd_t   *slots;    /**< borrowed: `capacity` command slots. */
    atomic_int  *states;   /**< borrowed: `capacity` slot flags (0=empty,1=ready). */
    uint32_t     capacity;
    atomic_uint  head;     /**< consumer ticket (monotonic; slot = head % cap). */
    atomic_uint  tail;     /**< next producer ticket (monotonic). */
} gpu_cmd_queue_t;

/**
 * @brief Initialise the queue over @p slots and @p states (each @p capacity
 *        long). Clears all slot flags to empty. No-op if any pointer is NULL or
 *        @p capacity is 0.
 */
void gpu_cmd_queue_init(gpu_cmd_queue_t *q, gpu_cmd_t *slots, atomic_int *states,
                        uint32_t capacity);

/**
 * @brief Enqueue a copy of @p cmd (multi-producer safe). Returns false if the
 *        queue is full or any argument is NULL; the queue is unchanged on false.
 */
bool gpu_cmd_push(gpu_cmd_queue_t *q, const gpu_cmd_t *cmd);

/**
 * @brief Dequeue the oldest command into @p out (single consumer only). Returns
 *        false if the queue is empty or any argument is NULL (@p out untouched).
 */
bool gpu_cmd_pop(gpu_cmd_queue_t *q, gpu_cmd_t *out);

/** @brief Approximate number of outstanding commands (tail - head). */
uint32_t gpu_cmd_queue_count(const gpu_cmd_queue_t *q);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_GPU_CMD_QUEUE_H */
