#include <stddef.h>

#include "ferrum/ecs/entity.h"

int ecs_entity_is_alive(const ecs_entity_pool_t *pool, entity_t entity) {
    if (pool == NULL || entity.index >= pool->capacity) {
        return 0;
    }
    return pool->generations[entity.index] == entity.generation;
}

uint32_t ecs_entity_live_count(const ecs_entity_pool_t *pool) {
    if (pool == NULL) {
        return 0u;
    }
    return pool->live_count;
}
