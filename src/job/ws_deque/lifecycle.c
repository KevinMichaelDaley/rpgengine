#include <stdlib.h>
#include <string.h>

#include "ferrum/job/ws_deque.h"

static size_t next_pow2_at_least_2(size_t x) {
    if (x < 2) {
        return 2;
    }

    size_t p = 1;
    while (p < x) {
        p <<= 1;
    }
    return p;
}

int fr_ws_deque_init(fr_ws_deque_t *dq, size_t capacity) {
    if (!dq) {
        return -1;
    }

    memset(dq, 0, sizeof(*dq));

    size_t cap = next_pow2_at_least_2(capacity);
    dq->buffer = (void **)calloc(cap, sizeof(void *));
    if (!dq->buffer) {
        return -1;
    }

    dq->capacity = cap;
    dq->mask = cap - 1;
    atomic_init(&dq->top, 0);
    atomic_init(&dq->bottom, 0);
    return 0;
}

void fr_ws_deque_destroy(fr_ws_deque_t *dq) {
    if (!dq) {
        return;
    }

    free(dq->buffer);
    dq->buffer = NULL;
    dq->capacity = 0;
    dq->mask = 0;
}
