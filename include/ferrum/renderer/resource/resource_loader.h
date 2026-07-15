/**
 * @file resource_loader.h
 * @brief Fiber-based asset loading: dispatch CPU-side decode work onto the job
 *        system, emit GPU commands for the render thread to realise.
 *
 * A load call reserves a registry handle immediately (resource starts
 * not-ready) and dispatches a fiber that reads + decodes the file into the
 * loader's thread-safe arena, then enqueues a create-texture command. The
 * caller waits on the counter, drains the executor on the render thread, then
 * resets the arena. GL is never touched off the render thread.
 *
 * Ownership: borrows the job system, command queue, registry and arena; the
 * decoded pixel data lives in the arena until the caller resets it (after drain).
 */
#ifndef FERRUM_RENDERER_RESOURCE_RESOURCE_LOADER_H
#define FERRUM_RENDERER_RESOURCE_RESOURCE_LOADER_H

#include <stdint.h>

#include "ferrum/job/counter.h"
#include "ferrum/job/system.h"
#include "ferrum/memory/arena.h"
#include "ferrum/renderer/resource/gpu_cmd_queue.h"
#include "ferrum/renderer/resource/gpu_registry.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Loader binding the job system + resource plumbing together. */
typedef struct resource_loader {
    job_system_t    *jobs;
    gpu_cmd_queue_t *queue;
    gpu_registry_t  *registry;
    arena_t         *arena;   /**< thread-safe scratch for decoded payloads. */
} resource_loader_t;

/** @brief Bind the loader to its subsystems (all borrowed). */
void resource_loader_init(resource_loader_t *ldr, job_system_t *jobs,
                          gpu_cmd_queue_t *queue, gpu_registry_t *registry,
                          arena_t *arena);

/**
 * @brief Asynchronously load an image file into a GPU texture. Reserves + returns
 *        the resource handle now (ready=0); a fiber decodes @p path and enqueues
 *        the create command. @p counter (auto-managed by the job system) reaches
 *        0 when the decode+enqueue completes. Returns @ref GPU_HANDLE_INVALID if
 *        the registry is full or an argument is NULL. @p path is borrowed and
 *        must outlive the job.
 */
uint64_t resource_loader_load_texture(resource_loader_t *ldr, const char *path,
                                      job_counter_t *counter);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_RESOURCE_RESOURCE_LOADER_H */
