#include <stdlib.h>

#include "ferrum/memory/pool.h"

void pool_destroy(pool_t *pool) {
    if (pool == NULL) {
        return;
    }
    free(pool->storage);
    free(pool->free_list);
    free(pool->generations);
    pool->storage = NULL;
    pool->free_list = NULL;
    pool->generations = NULL;
    pool->capacity = 0u;
    pool->stride = 0u;
    pool->free_head = POOL_INDEX_INVALID;
}
