#include <stddef.h>
#include <stdint.h>

#include "ferrum/ecs/entity.h"

ecs_status_t ecs_entity_destroy(ecs_entity_pool_t *pool, entity_t entity) {
    if (pool == NULL) {
        return ECS_ERR_INVALID;
    }
    if (entity.index >= pool->capacity) {
        return ECS_ERR_INVALID;
    }
    if (pool->generations[entity.index] != entity.generation) {
        return ECS_ERR_INVALID;
    }
    /* Saturate at UINT32_MAX to prevent wraparound reuse of stale handles. */
    if (pool->generations[entity.index] < UINT32_MAX) {
        pool->generations[entity.index]++;
    }
    pool->free_list[entity.index] = pool->free_head;
    pool->free_head = entity.index;
    if (pool->live_count > 0u) {
        pool->live_count--;
    }
    return ECS_OK;
}
