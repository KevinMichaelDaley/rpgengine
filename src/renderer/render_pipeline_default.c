#include "ferrum/renderer/render_pipeline.h"

int render_pipeline_default(render_pipeline_t *pipeline,
                            render_pass_t *skybox,
                            render_pass_t *forward,
                            render_pass_t *post) {
    if (pipeline == NULL || skybox == NULL || forward == NULL || post == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    static render_pass_t passes[3];
    passes[0] = *skybox;
    passes[1] = *forward;
    passes[2] = *post;
    pipeline->passes = passes;
    pipeline->pass_count = 3u;
    pipeline->glBindFramebuffer = NULL;
    return RENDER_PIPELINE_OK;
}
