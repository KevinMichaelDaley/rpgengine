/**
 * @file edit_cmd_ring_consume.c
 * @brief SPSC ring buffer consumer operations (peek, advance, pop).
 */

#include "ferrum/editor/edit_cmd_ring.h"
#include <string.h>

const edit_cmd_slot_t *edit_cmd_ring_peek(edit_cmd_ring_t *ring) {
    if (!ring) return NULL;

    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t head = atomic_load_explicit(&ring->head, memory_order_acquire);

    if (tail == head) return NULL; /* Empty. */

    uint32_t idx = tail & ring->mask;
    return &ring->slots[idx];
}

void edit_cmd_ring_advance(edit_cmd_ring_t *ring) {
    if (!ring) return;
    uint32_t tail = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    atomic_store_explicit(&ring->tail, tail + 1, memory_order_release);
}

bool edit_cmd_ring_pop(edit_cmd_ring_t *ring, edit_cmd_slot_t *out,
                       char *payload_buf, uint32_t payload_buf_cap) {
    if (!out) return false;

    const edit_cmd_slot_t *slot = edit_cmd_ring_peek(ring);
    if (!slot) return false;

    out->id          = slot->id;
    out->payload_len = slot->payload_len;
    out->payload     = payload_buf;

    /* Copy payload into caller's buffer. */
    if (payload_buf && payload_buf_cap > 0 && slot->payload_len > 0) {
        uint32_t copy_len = slot->payload_len < payload_buf_cap
                                ? slot->payload_len : payload_buf_cap;
        memcpy(payload_buf, slot->payload, copy_len);
    }

    edit_cmd_ring_advance(ring);
    return true;
}
