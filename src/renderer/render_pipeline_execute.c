#include "ferrum/renderer/render_pipeline.h"

int render_pipeline_execute(const render_pipeline_t *pipeline) {
    if (pipeline == NULL || pipeline->passes == NULL || pipeline->pass_count == 0u) {
        return RENDER_PIPELINE_ERR_INVALID;
    }
    for (size_t i = 0; i < pipeline->pass_count; ++i) {
        render_pass_t *pass = &pipeline->passes[i];
        if (pass->begin != NULL) {
            pass->begin(pass->user_data);
        }
        if (pass->submit != NULL) {
            pass->submit(pass->user_data);
        }
        if (pass->end != NULL) {
            pass->end(pass->user_data);
        }
    }
    return RENDER_PIPELINE_OK;
}
