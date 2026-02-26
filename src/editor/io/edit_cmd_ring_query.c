/**
 * @file edit_cmd_ring_query.c
 * @brief SPSC ring buffer query operations (count, empty).
 */

#include "ferrum/editor/edit_cmd_ring.h"

uint32_t edit_cmd_ring_count(const edit_cmd_ring_t *ring) {
    if (!ring) return 0;
    uint32_t head = atomic_load_explicit(
        (atomic_uint *)&ring->head, memory_order_acquire);
    uint32_t tail = atomic_load_explicit(
        (atomic_uint *)&ring->tail, memory_order_acquire);
    return head - tail;
}

bool edit_cmd_ring_empty(const edit_cmd_ring_t *ring) {
    return edit_cmd_ring_count(ring) == 0;
}
