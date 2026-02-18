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

static int apool_handle_valid(const apool_t *pool, apool_handle_t handle) {
    if (pool == NULL) {
        return 0;
    }
    if (handle.index >= pool->capacity) {
        return 0;
    }
    uint16_t curr = atomic_load(&pool->generations[handle.index]);
    if (curr != handle.generation) {
        return 0;
    }
    return 1;
}

apool_status_t apool_free(apool_t *pool, apool_handle_t handle) {
    if (!apool_handle_valid(pool, handle)) {
        return APOOL_ERR_INVALID;
    }
    // Generation increment semantics: wrap at UINT16_MAX back to 1.
    uint16_t old_gen, new_gen;
    do {
        old_gen = atomic_load_explicit(&pool->generations[handle.index], memory_order_relaxed);
        new_gen = (old_gen == UINT16_MAX) ? 1u : (uint16_t)(old_gen + 1u);
    } while (!atomic_compare_exchange_weak_explicit(&pool->generations[handle.index], 
                                                   &old_gen, new_gen, 
                                                   memory_order_relaxed, memory_order_relaxed));

    // Push onto lock-free stack.
    uint32_t idx = handle.index;
    uint64_t head = atomic_load_explicit(&pool->free_head, memory_order_acquire);
    for (;;) {
        uint32_t head_index = apool_head_index(head);
        uint32_t tag = apool_head_tag(head);
        uint64_t desired = apool_pack_head(idx, tag + 1u);
        pool->next[idx] = head_index;
        if (atomic_compare_exchange_weak_explicit(&pool->free_head,
                                                  &head,
                                                  desired,
                                                  memory_order_release,
                                                  memory_order_acquire)) {
            break;
        }
    }
    return APOOL_OK;
}
