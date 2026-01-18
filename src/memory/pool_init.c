#include <stdlib.h>

#include "ferrum/memory/pool.h"

pool_status_t pool_init(pool_t *pool, uint32_t capacity, uint32_t stride) {
    if (pool == NULL || capacity == 0u || stride == 0u) {
        return POOL_ERR_INVALID;
    }
    pool->storage = NULL;
    pool->free_list = NULL;
    pool->generations = NULL;
    pool->capacity = 0u;
    pool->stride = 0u;
    pool->free_head = POOL_INDEX_INVALID;
    pool->storage = calloc(capacity, stride);
    pool->free_list = calloc(capacity, sizeof(uint32_t));
    pool->generations = calloc(capacity, sizeof(uint16_t));
    if (pool->storage == NULL || pool->free_list == NULL || pool->generations == NULL) {
        free(pool->storage);
        free(pool->free_list);
        free(pool->generations);
        pool->storage = NULL;
        pool->free_list = NULL;
        pool->generations = NULL;
        return POOL_ERR_OOM;
    }
    pool->capacity = capacity;
    pool->stride = stride;
    pool->free_head = 0u;
    for (uint32_t i = 0; i < capacity; ++i) {
        pool->free_list[i] = i + 1u;
        pool->generations[i] = 1u;
    }
    pool->free_list[capacity - 1u] = POOL_INDEX_INVALID;
    return POOL_OK;
}
