#ifndef FERRUM_RENDERER_RENDER_PIPELINE_H
#define FERRUM_RENDERER_RENDER_PIPELINE_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/renderer/render_resource_views.h"

/** @file
 * @brief Render pipeline graph and pass interfaces.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Render pipeline status codes. */
#define RENDER_PIPELINE_OK 0
#define RENDER_PIPELINE_ERR_INVALID 1

/** Render pass descriptor. */
typedef struct render_pass {
    const char *name;
    void (*begin)(void *user_data);
    void (*submit)(void *user_data);
    void (*end)(void *user_data);
    void *user_data;
} render_pass_t;


/** Render pipeline with ordered passes. */
typedef struct render_pipeline {
    render_pass_t *passes;
    size_t pass_count;
    void (*glBindFramebuffer)(uint32_t target, uint32_t framebuffer);
} render_pipeline_t;

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
