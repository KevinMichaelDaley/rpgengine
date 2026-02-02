#include "ferrum/memory/apool.h"

void *apool_get(const apool_t *pool, apool_handle_t handle) {
    if (pool == NULL) {
        return NULL;
    }
    if (handle.index >= pool->capacity) {
        return NULL;
    }
    uint16_t curr = atomic_load(&pool->generations[handle.index]);
    if (curr != handle.generation) {
        return NULL;
    }
    return pool->storage + (size_t)handle.index * pool->stride;
}
