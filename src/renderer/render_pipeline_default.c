#include "ferrum/renderer/render_pipeline.h"

int render_pipeline_default(render_pipeline_t *pipeline,
                            render_pass_t *storage,
                            render_pass_t *skybox,
                            render_pass_t *forward,
                            render_pass_t *post) {
    if (pipeline == NULL || storage == NULL || skybox == NULL || forward == NULL || post == NULL) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    storage[0] = *skybox;
    storage[1] = *forward;
    storage[2] = *post;
    pipeline->passes = storage;
    pipeline->pass_count = 3u;
    pipeline->glBindFramebuffer = NULL;
    pipeline->owns_storage = 0;
    return RENDER_PIPELINE_OK;
}
