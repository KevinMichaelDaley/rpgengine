#include "ferrum/renderer/render_pipeline.h"

#include "ferrum/renderer/gl_constants.h"

static int render_resource_validate_views(const render_resource_view_set_t *view_set) {
    if (view_set == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (view_set->attachments == NULL && view_set->attachment_count > 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (view_set->transients == NULL && view_set->transient_count > 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (view_set->inputs == NULL && view_set->input_count > 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (view_set->outputs == NULL && view_set->output_count > 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    return RENDER_PIPELINE_OK;
}

int render_pipeline_bind_resources(render_pipeline_t *pipeline,
                                   size_t pass_index,
                                   const render_resource_view_set_t *view_set) {
    if (pipeline == NULL || view_set == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (pass_index >= pipeline->pass_count) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (render_resource_validate_views(view_set) != RENDER_PIPELINE_OK) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (view_set->attachment_count == 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    const render_resource_attachment_t *color = &view_set->attachments[0];
    if (color->type != RENDER_RESOURCE_ATTACHMENT_COLOR) {
        return RENDER_PIPELINE_ERR_INVALID;
    }

    if (color->handle == 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (pipeline->glBindFramebuffer == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }

    uint32_t framebuffer = 0u;
    for (size_t i = 0; i < view_set->attachment_count; ++i) {
        const render_resource_attachment_t *attachment = &view_set->attachments[i];
        if (attachment->handle == 0u) {
            return RENDER_PIPELINE_ERR_INVALID;
        }
        framebuffer = attachment->handle;
        if (attachment->type == RENDER_RESOURCE_ATTACHMENT_DEPTH) {
            break;
        }
    }
    if (framebuffer == 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }

    pipeline->glBindFramebuffer(GL_FRAMEBUFFER, framebuffer);

    (void)pass_index;
    return RENDER_PIPELINE_OK;
}

int render_pipeline_unbind_resources(render_pipeline_t *pipeline,
                                     size_t pass_index) {
    if (pipeline == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (pass_index >= pipeline->pass_count) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    if (pipeline->glBindFramebuffer == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    pipeline->glBindFramebuffer(GL_FRAMEBUFFER, 0);
    (void)pass_index;
    return RENDER_PIPELINE_OK;
}
