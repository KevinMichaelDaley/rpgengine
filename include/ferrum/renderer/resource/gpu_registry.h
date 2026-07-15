/**
 * @file gpu_registry.h
 * @brief Pool-backed handle table for GPU resources (no loop allocation).
 *
 * Wraps a @ref pool_t of @ref gpu_resource_t. Handles are generation-checked
 * 64-bit values (generation<<32 | index) so a stale handle to a freed/reused
 * slot resolves to NULL instead of aliasing a new resource. Storage is fixed at
 * @ref gpu_registry_init; alloc/free are freelist O(1) with no allocation.
 */
#ifndef FERRUM_RENDERER_RESOURCE_GPU_REGISTRY_H
#define FERRUM_RENDERER_RESOURCE_GPU_REGISTRY_H

#include <stdint.h>

#include "ferrum/memory/pool.h"
#include "ferrum/renderer/resource/gpu_resource.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel invalid handle (index = POOL_INDEX_INVALID; never resolves). */
#define GPU_HANDLE_INVALID ((uint64_t)UINT32_MAX)

/** Registry: a pool of GPU resource descriptors. */
typedef struct gpu_registry {
    pool_t pool;
} gpu_registry_t;

/**
 * @brief Initialise the registry with room for @p capacity resources. Returns 0
 *        on success, non-zero (POOL_ERR_*) on failure or NULL @p reg.
 */
int gpu_registry_init(gpu_registry_t *reg, uint32_t capacity);

/** @brief Release the registry's storage. NULL-safe. */
void gpu_registry_destroy(gpu_registry_t *reg);

/**
 * @brief Reserve a resource slot of @p kind. Returns a generation-checked handle
 *        or @ref GPU_HANDLE_INVALID if the pool is full or @p reg is NULL. The
 *        descriptor starts zeroed except @c kind (gl_name 0, ready 0).
 */
uint64_t gpu_registry_alloc(gpu_registry_t *reg, gpu_resource_kind_t kind);

/**
 * @brief Resolve @p handle to its descriptor, or NULL if stale/invalid/NULL.
 *        The pointer is mutable so the render thread can fill gl_name + ready.
 */
gpu_resource_t *gpu_registry_get(const gpu_registry_t *reg, uint64_t handle);

/** @brief Release @p handle's slot (bumps its generation). NULL/stale-safe. */
void gpu_registry_free(gpu_registry_t *reg, uint64_t handle);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_GPU_REGISTRY_H */
