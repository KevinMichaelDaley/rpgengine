#include <stdlib.h>

#include "ferrum/ecs/entity.h"

ecs_status_t ecs_entity_pool_init(ecs_entity_pool_t *pool, uint32_t capacity) {
    if (pool == NULL || capacity == 0u) {
        return ECS_ERR_INVALID;
    }
    pool->free_list = NULL;
    pool->generations = NULL;
    pool->capacity = 0u;
    pool->live_count = 0u;
    pool->free_head = ECS_SPARSE_INVALID;
    pool->free_list = calloc(capacity, sizeof(uint32_t));
    pool->generations = calloc(capacity, sizeof(uint32_t));
    if (pool->free_list == NULL || pool->generations == NULL) {
        free(pool->free_list);
        free(pool->generations);
        pool->free_list = NULL;
        pool->generations = NULL;
        return ECS_ERR_OOM;
    }
    pool->capacity = capacity;
    pool->live_count = 0u;
    pool->free_head = 0u;
    for (uint32_t i = 0; i < capacity; ++i) {
        pool->free_list[i] = i + 1u;
        pool->generations[i] = 1u;
    }
    pool->free_list[capacity - 1u] = ECS_SPARSE_INVALID;
    return ECS_OK;
}
