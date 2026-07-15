/**
 * @file gpu_executor.h
 * @brief Render/main-thread executor that drains a @ref gpu_cmd_queue and
 *        performs the actual GL for each command, updating the registry.
 *
 * This is the ONLY place GL is called for resource creation/upload/destruction.
 * Loader fibers enqueue commands; the render thread calls @ref gpu_executor_drain
 * once per frame (or until idle) to realise them on the GPU and flip each
 * resource's @c ready flag. Owns its GL entry points (loaded once at init).
 */
#ifndef FERRUM_RENDERER_RESOURCE_GPU_EXECUTOR_H
#define FERRUM_RENDERER_RESOURCE_GPU_EXECUTOR_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/resource/gpu_cmd_queue.h"
#include "ferrum/renderer/resource/gpu_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Render-thread command executor + its GL entry points. */
typedef struct gpu_executor {
    gpu_registry_t    *registry;/**< borrowed: descriptors updated in place. */
    const gl_loader_t *loader;  /**< passed to GPU_CMD_CUSTOM finalisers. */

    void (*glGenTextures)(int32_t, uint32_t *);
    void (*glDeleteTextures)(int32_t, const uint32_t *);
    void (*glBindTexture)(uint32_t, uint32_t);
    void (*glTexImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexImage3D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexSubImage2D)(uint32_t, int32_t, int32_t, int32_t, int32_t, int32_t, uint32_t, uint32_t, const void *);
    void (*glTexParameteri)(uint32_t, uint32_t, int32_t);
    void (*glGenerateMipmap)(uint32_t);
    void (*glGenBuffers)(int32_t, uint32_t *);
    void (*glDeleteBuffers)(int32_t, const uint32_t *);
    void (*glBindBuffer)(uint32_t, uint32_t);
    void (*glBufferData)(uint32_t, intptr_t, const void *, uint32_t);
} gpu_executor_t;

/**
 * @brief Load GL entry points from @p loader and bind the executor to @p reg.
 *        Returns false on NULL args or a missing entry point.
 */
bool gpu_executor_init(gpu_executor_t *exec, const gl_loader_t *loader,
                       gpu_registry_t *reg);

/**
 * @brief Pop and execute every queued command (render thread only). Returns the
 *        number of commands executed. Each create/upload fills the target
 *        descriptor's gl_name and sets @c ready = 1.
 */
uint32_t gpu_executor_drain(gpu_executor_t *exec, gpu_cmd_queue_t *queue);

/** @brief NULL-safe teardown (does not delete GPU objects; the registry owns those). */
void gpu_executor_destroy(gpu_executor_t *exec);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_GPU_EXECUTOR_H */
