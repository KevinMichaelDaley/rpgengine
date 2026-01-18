#include "ferrum/memory/pool.h"

pool_handle_t pool_alloc(pool_t *pool) {
    pool_handle_t invalid = {POOL_INDEX_INVALID, 0u, 0u};
    if (pool == NULL || pool->free_head == POOL_INDEX_INVALID) {
        return invalid;
    }
    uint32_t index = pool->free_head;
    pool->free_head = pool->free_list[index];

    pool_handle_t handle = {index, pool->generations[index], 0u};
    return handle;
}
