/**
 * @file gpu_cmd_queue.c
 * @brief Bounded lock-free MPSC command ring (see gpu_cmd_queue.h).
 */
#include "ferrum/renderer/resource/gpu_cmd_queue.h"

#include <sched.h>

/* Per-slot flag values. */
#define SLOT_EMPTY 0
#define SLOT_READY 1

void gpu_cmd_queue_init(gpu_cmd_queue_t *q, gpu_cmd_t *slots, atomic_int *states,
                        uint32_t capacity)
{
    if (q == NULL || slots == NULL || states == NULL || capacity == 0u)
        return;
    q->slots = slots;
    q->states = states;
    q->capacity = capacity;
    atomic_store_explicit(&q->head, 0u, memory_order_relaxed);
    atomic_store_explicit(&q->tail, 0u, memory_order_relaxed);
    for (uint32_t i = 0; i < capacity; ++i)
        atomic_store_explicit(&states[i], SLOT_EMPTY, memory_order_relaxed);
}

bool gpu_cmd_push(gpu_cmd_queue_t *q, const gpu_cmd_t *cmd)
{
    if (q == NULL || cmd == NULL || q->slots == NULL)
        return false;

    /* Reserve a monotonic ticket, failing if the ring is full. The window
     * [head, tail) never exceeds capacity outstanding commands. */
    uint32_t ticket = atomic_load_explicit(&q->tail, memory_order_relaxed);
    for (;;) {
        uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);
        if (ticket - head >= q->capacity)
            return false; /* full. */
        if (atomic_compare_exchange_weak_explicit(&q->tail, &ticket, ticket + 1u,
                                                  memory_order_acq_rel,
                                                  memory_order_relaxed))
            break;
        /* ticket was reloaded with the current tail by the failed CAS; retry. */
    }

    uint32_t slot = ticket % q->capacity;
    /* The slot's previous occupant (ticket - capacity) is < head once we reserved
     * (full check), so the consumer has drained it -- but wait for the flag to
     * settle to be safe under weak memory ordering. */
    while (atomic_load_explicit(&q->states[slot], memory_order_acquire) != SLOT_EMPTY)
        sched_yield();

    q->slots[slot] = *cmd;
    atomic_store_explicit(&q->states[slot], SLOT_READY, memory_order_release);
    return true;
}

bool gpu_cmd_pop(gpu_cmd_queue_t *q, gpu_cmd_t *out)
{
    if (q == NULL || out == NULL || q->slots == NULL)
        return false;

    /* Single consumer: no other thread advances head. */
    uint32_t head = atomic_load_explicit(&q->head, memory_order_relaxed);
    uint32_t slot = head % q->capacity;
    if (atomic_load_explicit(&q->states[slot], memory_order_acquire) != SLOT_READY)
        return false; /* empty (or the producer for this slot hasn't published). */

    *out = q->slots[slot];
    atomic_store_explicit(&q->states[slot], SLOT_EMPTY, memory_order_release);
    atomic_store_explicit(&q->head, head + 1u, memory_order_release);
    return true;
}

uint32_t gpu_cmd_queue_count(const gpu_cmd_queue_t *q)
{
    if (q == NULL)
        return 0u;
    uint32_t tail = atomic_load_explicit(&q->tail, memory_order_acquire);
    uint32_t head = atomic_load_explicit(&q->head, memory_order_acquire);
    return tail - head;
}
