#include "ferrum/memory/pool.h"

static int pool_handle_valid(const pool_t *pool, pool_handle_t handle) {
    if (pool == NULL) {
        return 0;
    }
    if (handle.index >= pool->capacity) {
        return 0;
    }
    if (pool->generations[handle.index] != handle.generation) {
        return 0;
    }
    return 1;
}

pool_status_t pool_free(pool_t *pool, pool_handle_t handle) {
    if (!pool_handle_valid(pool, handle)) {
        return POOL_ERR_INVALID;
    }
    if (pool->generations[handle.index] == UINT16_MAX) {
        pool->generations[handle.index] = 1u;
    } else {
        pool->generations[handle.index]++;
    }
    pool->free_list[handle.index] = pool->free_head;
    pool->free_head = handle.index;
    return POOL_OK;
}
