#include "ferrum/memory/apool.h"

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
    uint16_t g = atomic_load(&pool->generations[handle.index]);
    if (g == UINT16_MAX) {
        atomic_store(&pool->generations[handle.index], 1u);
    } else {
        atomic_store(&pool->generations[handle.index], (uint16_t)(g + 1u));
    }

    // Push onto lock-free stack.
    uint32_t head = atomic_load(&pool->free_head);
    uint32_t idx = handle.index;
    do {
        pool->next[idx] = head;
    } while (!atomic_compare_exchange_weak(&pool->free_head, &head, idx));
    return APOOL_OK;
}
