#include <stdint.h>

#include "ferrum/ecs/sparse_set.h"

ecs_status_t ecs_sparse_set_base_insert(ecs_sparse_set_base_t *base, entity_t entity, const void *value) {
    if (base == NULL || value == NULL) {
        return ECS_ERR_INVALID;
    }
    if (entity.index >= base->capacity) {
        return ECS_ERR_INVALID;
    }
    if (entity.generation == 0u) {
        return ECS_ERR_INVALID;
    }
    if (base->sparse[entity.index] != ECS_SPARSE_INVALID) {
        return ECS_ERR_EXISTS;
    }
    if (base->size >= base->capacity) {
        return ECS_ERR_FULL;
    }
    uint32_t dense_index = base->size++;
    base->sparse[entity.index] = dense_index;
    base->dense_entities[dense_index] = entity;
    uint8_t *dst = (uint8_t *)base->dense + (size_t)dense_index * base->stride;
    const uint8_t *src = (const uint8_t *)value;
    for (size_t i = 0; i < base->stride; ++i) {
        dst[i] = src[i];
    }
    return ECS_OK;
}
