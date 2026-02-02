#include "ferrum/memory/apool.h"

apool_handle_t apool_alloc(apool_t *pool) {
    apool_handle_t invalid = {APOOL_INDEX_INVALID, 0u, 0u};
    if (pool == NULL) {
        return invalid;
    }

    uint32_t head = atomic_load(&pool->free_head);
    while (head != APOOL_INDEX_INVALID) {
        uint32_t next = pool->next[head];
        if (atomic_compare_exchange_weak(&pool->free_head, &head, next)) {
            uint16_t gen = atomic_load(&pool->generations[head]);
            apool_handle_t handle = {head, gen, 0u};
            return handle;
        }
        // CAS failed; `head` now contains the current value, loop continues.
    }
    return invalid;
}
