/**
 * @file aegis_async_buffer.c
 * @brief MPSC async task buffer: init, destroy, count.
 */

#include "ferrum/aegis/aegis_async.h"

#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helpers                                                            */
/* ------------------------------------------------------------------ */

/** @brief Round up to next power of two. */
static uint32_t next_pow2(uint32_t v) {
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

/* ------------------------------------------------------------------ */
/* Lifecycle                                                          */
/* ------------------------------------------------------------------ */

bool aegis_async_buffer_init(aegis_async_buffer_t *buf, uint32_t capacity) {
    memset(buf, 0, sizeof(*buf));

    /* Minimum capacity of 2 (one usable slot). */
    if (capacity < 2) { capacity = 2; }
    uint32_t cap = next_pow2(capacity);

    aegis_async_task_t *slots = calloc(cap, sizeof(aegis_async_task_t));
    if (!slots) { return false; }

    _Atomic uint32_t *committed = calloc(cap, sizeof(_Atomic uint32_t));
    if (!committed) { free(slots); return false; }

    buf->slots     = slots;
    buf->committed = committed;
    buf->capacity  = cap;
    buf->mask      = cap - 1;
    atomic_store(&buf->head, 0);
    atomic_store(&buf->tail, 0);
    return true;
}

void aegis_async_buffer_destroy(aegis_async_buffer_t *buf) {
    free(buf->slots);
    free(buf->committed);
    memset(buf, 0, sizeof(*buf));
}

uint32_t aegis_async_buffer_count(const aegis_async_buffer_t *buf) {
    uint32_t h = atomic_load_explicit(
        (atomic_uint *)&buf->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(
        (atomic_uint *)&buf->tail, memory_order_acquire);
    return (h - t) & buf->mask;
}
