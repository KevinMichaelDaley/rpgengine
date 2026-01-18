#include "ferrum/memory/pool.h"

void *pool_get(const pool_t *pool, pool_handle_t handle) {
    if (pool == NULL) {
        return NULL;
    }
    if (handle.index >= pool->capacity) {
        return NULL;
    }
    if (pool->generations[handle.index] != handle.generation) {
        return NULL;
    }
    return pool->storage + (size_t)handle.index * pool->stride;
}
