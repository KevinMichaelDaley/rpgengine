#include <stdlib.h>

#include "ferrum/memory/apool.h"

static uint64_t apool_pack_head(uint32_t index, uint32_t tag) {
    return ((uint64_t)tag << 32) | (uint64_t)index;
}

void apool_destroy(apool_t *pool) {
    if (pool == NULL) {
        return;
    }
    free(pool->storage);
    free(pool->next);
    free(pool->generations);
    pool->storage = NULL;
    pool->next = NULL;
    pool->generations = NULL;
    pool->capacity = 0u;
    pool->stride = 0u;
    atomic_store(&pool->free_head, apool_pack_head(APOOL_INDEX_INVALID, 0u));
}
