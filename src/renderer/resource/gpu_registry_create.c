/**
 * @file gpu_registry_create.c
 * @brief GPU resource registry lifetime (see gpu_registry.h).
 */
#include "ferrum/renderer/resource/gpu_registry.h"

#include <stddef.h>

int gpu_registry_init(gpu_registry_t *reg, uint32_t capacity)
{
    if (reg == NULL)
        return POOL_ERR_INVALID;
    return (int)pool_init(&reg->pool, capacity, (uint32_t)sizeof(gpu_resource_t));
}

void gpu_registry_destroy(gpu_registry_t *reg)
{
    if (reg == NULL)
        return;
    pool_destroy(&reg->pool);
}
