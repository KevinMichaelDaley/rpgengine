#include <stdint.h>

#include "ferrum/ecs/sparse_set.h"

void *ecs_sparse_set_base_get(ecs_sparse_set_base_t *base, entity_t entity) {
    if (base == NULL || entity.index >= base->capacity) {
        return NULL;
    }
    uint32_t dense_index = base->sparse[entity.index];
    if (dense_index == ECS_SPARSE_INVALID) {
        return NULL;
    }
    if (base->dense_entities[dense_index].generation != entity.generation) {
        return NULL;
    }
    return (uint8_t *)base->dense + (size_t)dense_index * base->stride;
}
