/**
 * @file gpu_resource.h
 * @brief Descriptor for one GPU resource tracked by the resource registry.
 *
 * A resource is allocated (its handle + descriptor) by any thread, but the GL
 * object it names is created/filled by the render thread executing a @ref
 * gpu_cmd. @ref ready flips to 1 once the GPU object exists, so consumers can
 * tell a still-loading resource from a live one without touching GL.
 */
#ifndef FERRUM_RENDERER_RESOURCE_GPU_RESOURCE_H
#define FERRUM_RENDERER_RESOURCE_GPU_RESOURCE_H

#include <stdatomic.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** What a @ref gpu_resource names. */
typedef enum gpu_resource_kind {
    GPU_RESOURCE_NONE = 0,
    GPU_RESOURCE_TEXTURE,
    GPU_RESOURCE_BUFFER,
    GPU_RESOURCE_SHADOW_TARGET
} gpu_resource_kind_t;

/** GPU resource descriptor stored in the registry pool. */
typedef struct gpu_resource {
    gpu_resource_kind_t kind;
    uint32_t   gl_name;      /**< GL object id (0 until created on the render thread). */
    uint32_t   width, height;
    uint32_t   layers;
    uint32_t   format;       /**< GL internal format. */
    uint32_t   base_slice;   /**< first slice (shadow-target allocations). */
    uint32_t   slice_count;  /**< number of slices. */
    atomic_int ready;        /**< 0 = pending (loading), 1 = live on the GPU. */
} gpu_resource_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_GPU_RESOURCE_H */
