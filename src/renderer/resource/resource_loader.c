/**
 * @file resource_loader.c
 * @brief Fiber-based texture loading (see resource_loader.h).
 */
#include "ferrum/renderer/resource/resource_loader.h"

#include <sched.h>
#include <stddef.h>
#include <string.h>

#include "stb_image.h"

#include "ferrum/renderer/gl_constants.h"
#include "ferrum/renderer/resource/gpu_cmd.h"

/* Per-load job context, arena-allocated so it outlives the dispatch call. */
typedef struct rl_tex_job {
    resource_loader_t *loader;
    const char        *path;   /**< borrowed. */
    uint64_t           handle;
} rl_tex_job_t;

/* Fiber body: decode the image into the arena and enqueue a create command.
 * On any failure the resource simply stays not-ready (gl_name 0). */
static void rl_tex_job_fn(void *ud)
{
    rl_tex_job_t *j = (rl_tex_job_t *)ud;
    int w = 0, h = 0, n = 0;
    unsigned char *px = stbi_load(j->path, &w, &h, &n, 4);
    if (px == NULL)
        return;
    size_t sz = (size_t)w * (size_t)h * 4u;
    void *copy = arena_alloc(j->loader->arena, 16u, sz);
    if (copy != NULL) {
        memcpy(copy, px, sz);
        gpu_cmd_t c;
        memset(&c, 0, sizeof c);
        c.type = GPU_CMD_CREATE_TEXTURE;
        c.target = j->handle;
        c.a = (uint32_t)w; c.b = (uint32_t)h; c.c = GL_RGBA8; c.d = 1u;
        c.data = copy; c.data_size = sz;
        while (!gpu_cmd_push(j->loader->queue, &c))
            sched_yield(); /* render thread will drain and free space. */
    }
    stbi_image_free(px);
}

void resource_loader_init(resource_loader_t *ldr, job_system_t *jobs,
                          gpu_cmd_queue_t *queue, gpu_registry_t *registry,
                          arena_t *arena)
{
    if (ldr == NULL)
        return;
    ldr->jobs = jobs;
    ldr->queue = queue;
    ldr->registry = registry;
    ldr->arena = arena;
}

uint64_t resource_loader_load_texture(resource_loader_t *ldr, const char *path,
                                      job_counter_t *counter)
{
    if (ldr == NULL || ldr->jobs == NULL || ldr->queue == NULL ||
        ldr->registry == NULL || ldr->arena == NULL || path == NULL)
        return GPU_HANDLE_INVALID;

    uint64_t handle = gpu_registry_alloc(ldr->registry, GPU_RESOURCE_TEXTURE);
    if (handle == GPU_HANDLE_INVALID)
        return GPU_HANDLE_INVALID;

    rl_tex_job_t *job = (rl_tex_job_t *)arena_alloc(ldr->arena, 16u, sizeof *job);
    if (job == NULL)
        return GPU_HANDLE_INVALID;
    job->loader = ldr;
    job->path = path;
    job->handle = handle;
    job_dispatch(ldr->jobs, rl_tex_job_fn, job, 0, counter);
    return handle;
}
