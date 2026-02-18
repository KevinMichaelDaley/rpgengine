#include <stdlib.h>
#include <string.h>

#include "ferrum/memory/apool.h"

static uint64_t apool_pack_head(uint32_t index, uint32_t tag) {
    return ((uint64_t)tag << 32) | (uint64_t)index;
}

apool_status_t apool_init(apool_t *pool, uint32_t capacity, uint32_t stride) {
    if (pool == NULL || capacity == 0u || stride == 0u) {
        return APOOL_ERR_INVALID;
    }
    pool->storage = NULL;
    pool->next = NULL;
    pool->generations = NULL;
    pool->capacity = 0u;
    pool->stride = 0u;
    atomic_store(&pool->free_head, apool_pack_head(APOOL_INDEX_INVALID, 0u));

    pool->storage = calloc(capacity, stride);
    pool->next = calloc(capacity, sizeof(*pool->next));
    pool->generations = calloc(capacity, sizeof(*pool->generations));
    if (pool->storage == NULL || pool->next == NULL || pool->generations == NULL) {
        free(pool->storage);
        free(pool->next);
        free(pool->generations);
        pool->storage = NULL;
        pool->next = NULL;
        pool->generations = NULL;
        return APOOL_ERR_OOM;
    }

    pool->capacity = capacity;
    pool->stride = stride;
    for (uint32_t i = 0; i < capacity; ++i) {
        pool->next[i] = i + 1u;
        atomic_store(&pool->generations[i], 1u);
    }
    pool->next[capacity - 1u] = APOOL_INDEX_INVALID;
    atomic_store(&pool->free_head, apool_pack_head(0u, 0u));
    return APOOL_OK;
}
