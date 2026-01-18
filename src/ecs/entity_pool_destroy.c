#include <stdlib.h>

#include "ferrum/ecs/entity.h"

void ecs_entity_pool_destroy(ecs_entity_pool_t *pool) {
    if (pool == NULL) {
        return;
    }
    free(pool->free_list);
    free(pool->generations);
    pool->free_list = NULL;
    pool->generations = NULL;
    pool->capacity = 0u;
    pool->live_count = 0u;
    pool->free_head = ECS_SPARSE_INVALID;
}
