#ifndef FERRUM_RENDERER_RENDER_PIPELINE_H
#define FERRUM_RENDERER_RENDER_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/render_pass_type.h"
#include "ferrum/renderer/render_resource_views.h"

/** @file
 * @brief Render pipeline: ordered pass execution with per-pass draw lists.
 *
 * Two usage modes:
 *  1. **Legacy** — render_pipeline_default() with external storage.
 *  2. **Full** — render_pipeline_init() allocates 9 passes with draw lists.
 *
 * In full mode, passes are indexed by render_pass_type_t and execute
 * in enum order (shadow → … → UI).
 */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward-declare draw_list_t to avoid circular include. */
struct draw_list;
typedef struct draw_list draw_list_t;

/** Render pipeline status codes. */
#define RENDER_PIPELINE_OK 0
#define RENDER_PIPELINE_ERR_INVALID 1

/** Render pass descriptor. */
typedef struct render_pass {
    const char *name;                       /**< Human-readable pass name. */
    void (*begin)(void *user_data);         /**< Called before submit. */
    void (*submit)(void *user_data);        /**< Draw submission callback. */
    void (*end)(void *user_data);           /**< Called after submit. */
    void *user_data;                        /**< Opaque context for callbacks. */
    render_pass_type_t pass_type;           /**< Pass type (enum index). */
    draw_list_t *draw_list;                 /**< Per-pass draw list (may be NULL). */
    uint32_t framebuffer;                   /**< FBO id (0 = default). */
} render_pass_t;


/** Render pipeline with ordered passes. */
typedef struct render_pipeline {
    render_pass_t *passes;                  /**< Array of passes. */
    size_t pass_count;                      /**< Number of passes. */
    void (*glBindFramebuffer)(uint32_t target, uint32_t framebuffer);
    int owns_storage;                       /**< Non-zero if init allocated. */
} render_pipeline_t;

/* ── Full pipeline (9-pass architecture) ──────────────────────────── */

/**
 * @brief Initialize a full 9-pass pipeline with per-pass draw lists.
 *
 * Allocates passes and draw lists in a single malloc.  Each draw list
 * has the specified capacity.  All callbacks start as NULL.
 *
 * @param pipeline        Pipeline to initialize (non-NULL).
 * @param draw_list_capacity  Per-pass draw list capacity (> 0).
 * @return Status code.
 */
int render_pipeline_init(render_pipeline_t *pipeline,
                         uint32_t draw_list_capacity);

/**
 * @brief Destroy a pipeline, freeing all owned resources.
 *
 * Safe to call with NULL.  Only frees if the pipeline owns its storage
 * (i.e., was created via render_pipeline_init).
 *
 * @param pipeline  Pipeline to destroy (NULL-safe).
 */
void render_pipeline_destroy(render_pipeline_t *pipeline);

/**
 * @brief Get a mutable pointer to a pass by type.
 *
 * @param pipeline  Pipeline (non-NULL).
 * @param type      Pass type (must be < RENDER_PASS_TYPE_COUNT).
 * @return Pointer to the pass, or NULL if type is out of range.
 */
render_pass_t *render_pipeline_get_pass(render_pipeline_t *pipeline,
                                        render_pass_type_t type);

/**
 * @brief Clear all per-pass draw lists (reset counts to zero).
 *
 * Call once per frame before populating draw lists.
 *
 * @param pipeline  Pipeline (non-NULL, NULL is a no-op).
 */
void render_pipeline_clear_draw_lists(render_pipeline_t *pipeline);

/* ── Legacy pipeline ──────────────────────────────────────────────── */

/**
 * @brief Initialize a default pipeline with skybox, forward, and post passes.
 * @param pipeline Pipeline output.
 * @param storage Storage for pass array (length 3).
 * @param skybox Skybox pass (non-NULL).
 * @param forward Forward pass (non-NULL).
 * @param post Post pass (non-NULL).
 * @return Status code.
 */
int render_pipeline_default(render_pipeline_t *pipeline,
                            render_pass_t *storage,
                            render_pass_t *skybox,
                            render_pass_t *forward,
                            render_pass_t *post);

/**
 * @brief Execute all passes in the pipeline.
 * @param pipeline Pipeline to execute.
 * @return Status code.
 */
int render_pipeline_execute(const render_pipeline_t *pipeline);

/**
 * @brief Bind resource views for a pipeline pass.
 * @param pipeline Pipeline pointer.
 * @param pass_index Pass index to bind.
 * @param view_set Resource view set definition.
 * @return Status code.
 */
int render_pipeline_bind_resources(render_pipeline_t *pipeline,
                                   size_t pass_index,
                                   const render_resource_view_set_t *view_set);

/**
 * @brief Unbind resources for a pipeline pass.
 * @param pipeline Pipeline pointer.
 * @param pass_index Pass index to unbind.
 * @return Status code.
 */
int render_pipeline_unbind_resources(render_pipeline_t *pipeline,
                                     size_t pass_index);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_RENDER_PIPELINE_H */
