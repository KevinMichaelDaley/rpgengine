#include <stddef.h>

#include "ferrum/ecs/entity.h"

ecs_status_t ecs_entity_create(ecs_entity_pool_t *pool, entity_t *out_entity) {
    if (pool == NULL || out_entity == NULL) {
        return ECS_ERR_INVALID;
    }
    if (pool->free_head == ECS_SPARSE_INVALID) {
        return ECS_ERR_FULL;
    }
    uint32_t index = pool->free_head;
    pool->free_head = pool->free_list[index];
    pool->live_count++;
    out_entity->index = index;
    out_entity->generation = pool->generations[index];
    return ECS_OK;
}
