#include <stdatomic.h>

#include "ferrum/job/ws_deque.h"

int fr_ws_deque_push(fr_ws_deque_t *dq, void *item) {
    if (!dq || !dq->buffer) {
        return -1;
    }

     /* Conservative ordering: use seq_cst atomics throughout.
         This trades a bit of performance for robustness on weak-memory architectures. */
     size_t b = atomic_load_explicit(&dq->bottom, memory_order_seq_cst);
     size_t t = atomic_load_explicit(&dq->top, memory_order_seq_cst);

    if ((b - t) >= dq->capacity) {
        return -1; /* full */
    }

    dq->buffer[b & dq->mask] = item;
    atomic_store_explicit(&dq->bottom, b + 1, memory_order_seq_cst);
    return 0;
}

void *fr_ws_deque_pop(fr_ws_deque_t *dq) {
    if (!dq || !dq->buffer) {
        return NULL;
    }

    size_t b = atomic_load_explicit(&dq->bottom, memory_order_seq_cst);
    if (b == 0) {
        return NULL;
    }

    b = b - 1;
    atomic_store_explicit(&dq->bottom, b, memory_order_seq_cst);

    size_t t = atomic_load_explicit(&dq->top, memory_order_seq_cst);
    if (t > b) {
        /* Empty. Restore invariant bottom == top. */
        atomic_store_explicit(&dq->bottom, t, memory_order_seq_cst);
        return NULL;
    }

    void *item = dq->buffer[b & dq->mask];

    if (t == b) {
        /* Last element: race with thieves. */
        size_t expected = t;
        if (!atomic_compare_exchange_strong_explicit(&dq->top, &expected, t + 1,
                                                    memory_order_seq_cst, memory_order_seq_cst)) {
            /* Lost race: stolen by thief. */
            item = NULL;
        }
        atomic_store_explicit(&dq->bottom, t + 1, memory_order_seq_cst);
    }

    return item;
}

void *fr_ws_deque_steal(fr_ws_deque_t *dq) {
    if (!dq || !dq->buffer) {
        return NULL;
    }

    size_t t = atomic_load_explicit(&dq->top, memory_order_seq_cst);
    size_t b = atomic_load_explicit(&dq->bottom, memory_order_seq_cst);

    if (t >= b) {
        return NULL;
    }

    void *item = dq->buffer[t & dq->mask];
    size_t expected = t;
    if (!atomic_compare_exchange_strong_explicit(&dq->top, &expected, t + 1,
                                                memory_order_seq_cst, memory_order_seq_cst)) {
        return NULL;
    }

    return item;
}
