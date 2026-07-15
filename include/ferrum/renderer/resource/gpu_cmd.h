/**
 * @file gpu_cmd.h
 * @brief A single GPU command: the unit of work handed from a loader fiber to
 *        the render/main thread.
 *
 * The renderer's resource paradigm: CPU-side asset work (file I/O, decode,
 * gather) runs on job-system FIBERS, but every GL/API call must run on the
 * thread that owns the GL context. Fibers therefore never touch GL directly --
 * they produce @ref gpu_cmd_t records and enqueue them (see @ref gpu_cmd_queue),
 * and the render thread drains + executes them.
 *
 * A command is plain data (no ownership of @ref data, which is borrowed from the
 * producing fiber and must outlive execution -- typically fiber-arena memory).
 */
#ifndef FERRUM_RENDERER_RESOURCE_GPU_CMD_H
#define FERRUM_RENDERER_RESOURCE_GPU_CMD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Kind of GPU operation a @ref gpu_cmd_t requests. */
typedef enum gpu_cmd_type {
    GPU_CMD_NONE = 0,        /**< no-op / cleared slot. */
    GPU_CMD_CREATE_TEXTURE,  /**< allocate a texture (a=w,b=h,c=format,d=layers). */
    GPU_CMD_UPLOAD_TEXTURE,  /**< upload pixels (data/data_size) to @ref target. */
    GPU_CMD_DESTROY_TEXTURE, /**< release @ref target's texture. */
    GPU_CMD_CREATE_BUFFER,   /**< allocate a buffer (a=size,b=usage). */
    GPU_CMD_UPLOAD_BUFFER,   /**< upload data to @ref target's buffer. */
    GPU_CMD_DESTROY_BUFFER,  /**< release @ref target's buffer. */
    GPU_CMD_ALLOC_SHADOW,    /**< reserve shadow depth-target slices (a=count). */
    GPU_CMD_FREE_SHADOW,     /**< release shadow slices (a=base,b=count). */
    GPU_CMD_CUSTOM           /**< run @ref gpu_cmd::execute on the render thread. */
} gpu_cmd_type_t;

/** One deferred GPU operation. Generic scalar params keep the record POD and
 *  copyable through the ring; @ref data is borrowed CPU memory for uploads. */
typedef struct gpu_cmd {
    gpu_cmd_type_t type;
    uint64_t       target;    /**< resource handle the command affects/produces. */
    uint32_t       a, b, c, d;/**< op-specific scalars (see @ref gpu_cmd_type). */
    const void    *data;      /**< borrowed CPU payload for uploads (may be NULL). */
    size_t         data_size; /**< bytes at @ref data. */
    /* GPU_CMD_CUSTOM: a fiber-prepared finaliser the executor runs on the render
     * thread. @c user receives the executor's gl_loader_t*, so the callback can
     * call the renderer's own creation functions (texture_create,
     * static_mesh_create, ...) to produce real texture_t / static_mesh_t. */
    void (*execute)(void *ctx, void *user);
    void  *ctx;               /**< borrowed context for @ref execute. */
} gpu_cmd_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_GPU_CMD_H */
