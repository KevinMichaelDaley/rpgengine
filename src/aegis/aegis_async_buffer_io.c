/**
 * @file aegis_async_buffer_io.c
 * @brief MPSC async task buffer: submit (producer) and drain (consumer).
 *
 * The MPSC pattern uses atomic CAS on head for producers. Each slot has
 * a per-slot committed flag to prevent the consumer from reading a slot
 * that a producer has claimed (CAS) but not yet finished writing.
 */

#include "ferrum/aegis/aegis_async.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Producer — lock-free CAS on head                                   */
/* ------------------------------------------------------------------ */

bool aegis_async_buffer_submit(aegis_async_buffer_t *buf,
                               const aegis_async_task_t *task) {
    uint32_t head;
    uint32_t next;

    for (;;) {
        head = atomic_load_explicit(&buf->head, memory_order_relaxed);
        next = (head + 1) & buf->mask;

        /* Check if full: next write position == tail (consumer position). */
        uint32_t tail = atomic_load_explicit(&buf->tail,
                                             memory_order_acquire);
        if (next == tail) {
            return false; /* Buffer full. */
        }

        /* Try to claim this slot. */
        if (atomic_compare_exchange_weak_explicit(
                &buf->head, &head, next,
                memory_order_acq_rel,
                memory_order_relaxed)) {
            break;
        }
        /* CAS failed — another producer won; retry. */
    }

    /* Copy task into the claimed slot. */
    buf->slots[head] = *task;
    atomic_store_explicit(&buf->slots[head].status, AEGIS_ASYNC_PENDING,
                          memory_order_relaxed);

    /* Signal that this slot is fully written and ready to be consumed. */
    atomic_store_explicit(&buf->committed[head], 1, memory_order_release);
    return true;
}

/* ------------------------------------------------------------------ */
/* Consumer — single reader, no CAS needed                            */
/* ------------------------------------------------------------------ */

uint32_t aegis_async_buffer_drain(aegis_async_buffer_t *buf,
                                  aegis_async_task_t *out_tasks,
                                  uint32_t max_tasks) {
    uint32_t tail = atomic_load_explicit(&buf->tail, memory_order_relaxed);
    uint32_t count = 0;

    while (count < max_tasks) {
        uint32_t head = atomic_load_explicit(&buf->head,
                                             memory_order_acquire);
        uint32_t idx = (tail + count) & buf->mask;

        /* Check if there are any more entries at all. */
        if (((tail + count) & buf->mask) == (head & buf->mask) &&
            ((tail + count) == head)) {
            break;
        }
        /* More precisely: empty when tail+count == head. */
        if (tail + count == head) {
            break;
        }

        /* Check if this slot has been fully written by its producer. */
        if (!atomic_load_explicit(&buf->committed[idx],
                                  memory_order_acquire)) {
            /* Producer claimed but hasn't finished writing. Stop here
             * to maintain FIFO ordering. */
            break;
        }

        /* Read the slot and clear the committed flag. */
        out_tasks[count] = buf->slots[idx];
        atomic_store_explicit(&buf->committed[idx], 0,
                              memory_order_relaxed);
        count++;
    }

    if (count > 0) {
        uint32_t new_tail = (tail + count) & buf->mask;
        atomic_store_explicit(&buf->tail, new_tail, memory_order_release);
    }

    return count;
}
