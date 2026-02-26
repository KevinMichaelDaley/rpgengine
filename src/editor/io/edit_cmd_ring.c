/**
 * @file edit_cmd_ring.c
 * @brief Lock-free SPSC ring buffer — lifecycle and producer operations.
 */

#include "ferrum/editor/edit_cmd_ring.h"
#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------------- */
/* Helpers                                                                   */
/* ----------------------------------------------------------------------- */

/** @brief Round up to the next power of 2 (or return n if already a power). */
static uint32_t next_pow2_(uint32_t n) {
    if (n == 0) return 1;
    n--;
    n |= n >> 1;
    n |= n >> 2;
    n |= n >> 4;
    n |= n >> 8;
    n |= n >> 16;
    return n + 1;
}

/* ----------------------------------------------------------------------- */
/* Lifecycle                                                                 */
/* ----------------------------------------------------------------------- */

bool edit_cmd_ring_init(edit_cmd_ring_t *ring, uint32_t capacity,
                        uint32_t max_payload) {
    if (!ring || capacity == 0 || max_payload == 0) return false;

    uint32_t cap = next_pow2_(capacity);
    ring->capacity    = cap;
    ring->payload_cap = max_payload;
    ring->mask        = cap - 1;

    ring->slots = (edit_cmd_slot_t *)calloc(cap, sizeof(edit_cmd_slot_t));
    if (!ring->slots) return false;

    /* Allocate one contiguous payload buffer for all slots. */
    ring->payload_buf = (char *)malloc((size_t)cap * max_payload);
    if (!ring->payload_buf) {
        free(ring->slots);
        ring->slots = NULL;
        return false;
    }

    /* Point each slot's payload to its region of the contiguous buffer. */
    for (uint32_t i = 0; i < cap; ++i) {
        ring->slots[i].payload = ring->payload_buf + (size_t)i * max_payload;
    }

    atomic_store_explicit(&ring->head, 0, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, 0, memory_order_relaxed);
    return true;
}

void edit_cmd_ring_destroy(edit_cmd_ring_t *ring) {
    if (!ring) return;
    free(ring->slots);
    free(ring->payload_buf);
    ring->slots       = NULL;
    ring->payload_buf = NULL;
    ring->capacity    = 0;
}

/* ----------------------------------------------------------------------- */
/* Producer                                                                  */
/* ----------------------------------------------------------------------- */

bool edit_cmd_ring_push(edit_cmd_ring_t *ring, uint32_t id,
                        const char *payload, uint32_t payload_len) {
    if (!ring || payload_len > ring->payload_cap) return false;

    uint32_t head = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_acquire);

    /* Full if head is one full lap ahead of tail. */
    if (head - tail >= ring->capacity) return false;

    uint32_t idx = head & ring->mask;
    ring->slots[idx].id          = id;
    ring->slots[idx].payload_len = payload_len;
    if (payload && payload_len > 0) {
        memcpy(ring->slots[idx].payload, payload, payload_len);
    }

    /* Release so consumer sees the written data. */
    atomic_store_explicit(&ring->head, head + 1, memory_order_release);
    return true;
}
