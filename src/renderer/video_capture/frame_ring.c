/**
 * @file frame_ring.c
 * @brief SPSC lock-free ring buffer for captured frame pixel data.
 */

#include "frame_ring.h"
#include <stdlib.h>
#include <string.h>

void fr_frame_ring_init(fr_frame_ring_t *ring, uint32_t frame_bytes) {
    if (!ring) { return; }
    atomic_init(&ring->head, 0);
    atomic_init(&ring->tail, 0);
    for (int i = 0; i < FR_FRAME_RING_CAPACITY; i++) {
        ring->slots[i].pixels = (uint8_t *)malloc(frame_bytes);
        ring->slots[i].frame_bytes = frame_bytes;
    }
}

void fr_frame_ring_destroy(fr_frame_ring_t *ring) {
    if (!ring) { return; }
    for (int i = 0; i < FR_FRAME_RING_CAPACITY; i++) {
        free(ring->slots[i].pixels);
        ring->slots[i].pixels = NULL;
    }
}

int fr_frame_ring_push(fr_frame_ring_t *ring,
                       const uint8_t *pixels, uint32_t frame_bytes) {
    if (!ring || !pixels) { return 0; }

    uint32_t h = atomic_load_explicit(&ring->head, memory_order_relaxed);
    uint32_t t = atomic_load_explicit(&ring->tail, memory_order_acquire);
    int dropped = 0;

    /* If full, advance tail (drop oldest frame). */
    if (h - t >= FR_FRAME_RING_CAPACITY) {
        atomic_store_explicit(&ring->tail, t + 1, memory_order_release);
        dropped = 1;
    }

    uint32_t idx = h & (FR_FRAME_RING_CAPACITY - 1);
    uint32_t copy_bytes = frame_bytes < ring->slots[idx].frame_bytes
                              ? frame_bytes
                              : ring->slots[idx].frame_bytes;
    memcpy(ring->slots[idx].pixels, pixels, copy_bytes);

    atomic_store_explicit(&ring->head, h + 1, memory_order_release);
    return dropped ? 0 : 1;
}

const uint8_t *fr_frame_ring_pop(fr_frame_ring_t *ring,
                                 uint32_t *out_bytes) {
    if (!ring) { return NULL; }

    uint32_t t = atomic_load_explicit(&ring->tail, memory_order_relaxed);
    uint32_t h = atomic_load_explicit(&ring->head, memory_order_acquire);

    if (t >= h) { return NULL; } /* Empty. */

    uint32_t idx = t & (FR_FRAME_RING_CAPACITY - 1);
    if (out_bytes) { *out_bytes = ring->slots[idx].frame_bytes; }

    /* Advance tail after caller is done — but since we return a pointer
     * into the slot, we advance now.  The slot won't be reused until
     * head wraps around (capacity slots away). */
    atomic_store_explicit(&ring->tail, t + 1, memory_order_release);
    return ring->slots[idx].pixels;
}

uint32_t fr_frame_ring_count(const fr_frame_ring_t *ring) {
    if (!ring) { return 0; }
    uint32_t h = atomic_load_explicit(
        &((fr_frame_ring_t *)ring)->head, memory_order_acquire);
    uint32_t t = atomic_load_explicit(
        &((fr_frame_ring_t *)ring)->tail, memory_order_acquire);
    return h - t;
}
