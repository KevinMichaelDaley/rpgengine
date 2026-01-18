#include <stdlib.h>

#include "ferrum/ecs/sparse_set.h"

ecs_status_t ecs_sparse_set_base_init(ecs_sparse_set_base_t *base, uint32_t capacity, size_t stride) {
    if (base == NULL || capacity == 0u || stride == 0u) {
        return ECS_ERR_INVALID;
    }
    base->dense = calloc(capacity, stride);
    base->dense_entities = calloc(capacity, sizeof(entity_t));
    base->sparse = calloc(capacity, sizeof(uint32_t));
    if (base->dense == NULL || base->dense_entities == NULL || base->sparse == NULL) {
        free(base->dense);
        free(base->dense_entities);
        free(base->sparse);
        base->dense = NULL;
        base->dense_entities = NULL;
        base->sparse = NULL;
        return ECS_ERR_OOM;
    }
    base->capacity = capacity;
    base->size = 0u;
    base->stride = stride;
    for (uint32_t i = 0; i < capacity; ++i) {
        base->sparse[i] = ECS_SPARSE_INVALID;
    }
    return ECS_OK;
}
