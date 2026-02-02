#include <stdlib.h>

#include "ferrum/memory/apool.h"

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
    atomic_store(&pool->free_head, APOOL_INDEX_INVALID);
}
