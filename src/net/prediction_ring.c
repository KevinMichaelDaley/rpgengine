/**
 * @file prediction_ring.c
 * @brief Input ring buffer for client prediction.
 *
 * Non-static functions: 3 (init, push, get).
 */

#include "ferrum/net/prediction.h"
#include <string.h>

void net_predict_input_ring_init(net_predict_input_ring_t *ring,
                                 net_predict_input_t *entries,
                                 uint32_t capacity) {
    if (!ring) { return; }
    ring->entries = entries;
    ring->capacity = capacity;
    ring->write_idx = 0;
    ring->count = 0;
    if (entries && capacity > 0) {
        memset(entries, 0, capacity * sizeof(net_predict_input_t));
    }
}

int net_predict_input_ring_push(net_predict_input_ring_t *ring,
                                const net_predict_input_t *input) {
    if (!ring || !input || !ring->entries) {
        return NET_PREDICT_ERR_INVALID;
    }

    ring->entries[ring->write_idx] = *input;
    ring->write_idx = (ring->write_idx + 1) % ring->capacity;
    if (ring->count < ring->capacity) {
        ring->count++;
    }

    return NET_PREDICT_OK;
}

const net_predict_input_t *net_predict_input_ring_get(
    const net_predict_input_ring_t *ring, uint32_t tick) {
    if (!ring || !ring->entries) { return NULL; }

    /* Search the ring for matching tick. */
    uint32_t n = ring->count;
    for (uint32_t i = 0; i < n; i++) {
        /* Walk backwards from the most recent write. */
        uint32_t idx = (ring->write_idx + ring->capacity - 1 - i)
                       % ring->capacity;
        if (ring->entries[idx].tick == tick) {
            return &ring->entries[idx];
        }
    }
    return NULL;
}
