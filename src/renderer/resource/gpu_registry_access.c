/**
 * @file gpu_registry_access.c
 * @brief GPU resource registry alloc/get/free (see gpu_registry.h).
 */
#include "ferrum/renderer/resource/gpu_registry.h"

#include <stddef.h>
#include <string.h>

/* Pack/unpack a pool handle into an opaque 64-bit resource handle. */
static uint64_t gpu_handle_pack(pool_handle_t h)
{
    return ((uint64_t)h.generation << 32) | (uint64_t)h.index;
}

static pool_handle_t gpu_handle_unpack(uint64_t v)
{
    pool_handle_t h;
    h.index = (uint32_t)(v & 0xFFFFFFFFu);
    h.generation = (uint16_t)((v >> 32) & 0xFFFFu);
    h.flags = 0u;
    return h;
}

uint64_t gpu_registry_alloc(gpu_registry_t *reg, gpu_resource_kind_t kind)
{
    if (reg == NULL)
        return GPU_HANDLE_INVALID;
    pool_handle_t h = pool_alloc(&reg->pool);
    if (h.index == POOL_INDEX_INVALID)
        return GPU_HANDLE_INVALID;
    gpu_resource_t *r = (gpu_resource_t *)pool_get(&reg->pool, h);
    if (r == NULL)
        return GPU_HANDLE_INVALID;
    memset(r, 0, sizeof(*r));
    r->kind = kind;
    atomic_store_explicit(&r->ready, 0, memory_order_relaxed);
    return gpu_handle_pack(h);
}

gpu_resource_t *gpu_registry_get(const gpu_registry_t *reg, uint64_t handle)
{
    if (reg == NULL || handle == GPU_HANDLE_INVALID)
        return NULL;
    return (gpu_resource_t *)pool_get(&reg->pool, gpu_handle_unpack(handle));
}

void gpu_registry_free(gpu_registry_t *reg, uint64_t handle)
{
    if (reg == NULL || handle == GPU_HANDLE_INVALID)
        return;
    pool_free(&reg->pool, gpu_handle_unpack(handle));
}
