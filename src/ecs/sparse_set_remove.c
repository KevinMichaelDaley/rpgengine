#include <stdint.h>

#include "ferrum/ecs/sparse_set.h"

ecs_status_t ecs_sparse_set_base_remove(ecs_sparse_set_base_t *base, entity_t entity) {
    if (base == NULL) {
        return ECS_ERR_INVALID;
    }
    if (entity.index >= base->capacity) {
        return ECS_ERR_INVALID;
    }
    uint32_t dense_index = base->sparse[entity.index];
    if (dense_index == ECS_SPARSE_INVALID) {
        return ECS_ERR_NOT_FOUND;
    }
    if (base->dense_entities[dense_index].generation != entity.generation) {
        return ECS_ERR_INVALID;
    }
    uint32_t last = base->size - 1u;
    if (dense_index != last) {
        uint8_t *dst = (uint8_t *)base->dense + (size_t)dense_index * base->stride;
        uint8_t *src = (uint8_t *)base->dense + (size_t)last * base->stride;
        for (size_t i = 0; i < base->stride; ++i) {
            dst[i] = src[i];
        }
        entity_t moved = base->dense_entities[last];
        base->dense_entities[dense_index] = moved;
        base->sparse[moved.index] = dense_index;
    }
    base->sparse[entity.index] = ECS_SPARSE_INVALID;
    base->size = last;
    return ECS_OK;
}
