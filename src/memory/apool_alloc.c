#include "ferrum/memory/apool.h"

static uint64_t apool_pack_head(uint32_t index, uint32_t tag) {
    return ((uint64_t)tag << 32) | (uint64_t)index;
}

static uint32_t apool_head_index(uint64_t head) {
    return (uint32_t)head;
}

static uint32_t apool_head_tag(uint64_t head) {
    return (uint32_t)(head >> 32);
}

apool_handle_t apool_alloc(apool_t *pool) {
    apool_handle_t invalid = {APOOL_INDEX_INVALID, 0u, 0u};
    if (pool == NULL) {
        return invalid;
    }

    uint64_t head = atomic_load_explicit(&pool->free_head, memory_order_acquire);
    for (;;) {
        uint32_t index = apool_head_index(head);
        uint32_t tag = apool_head_tag(head);
        if (index == APOOL_INDEX_INVALID) {
            return invalid;
        }

        uint32_t next = pool->next[index];
        uint64_t desired = apool_pack_head(next, tag + 1u);
        if (atomic_compare_exchange_weak_explicit(&pool->free_head,
                                                  &head,
                                                  desired,
                                                  memory_order_acq_rel,
                                                  memory_order_acquire)) {
            uint16_t gen = atomic_load_explicit(&pool->generations[index], memory_order_relaxed);
            apool_handle_t handle = {index, gen, 0u};
            return handle;
        }
        /* CAS failed; `head` now contains the current value, loop continues. */
    }
}
