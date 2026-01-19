#include "ferrum/renderer/skinning/pipeline.h"

#define SKINNING_PIPELINE_INVALID_INDEX UINT32_MAX

int skinning_pipeline_palette_index(const skinning_pipeline_t *pipeline,
                                   entity_t entity,
                                   uint32_t *out_index) {
    if (pipeline == NULL || out_index == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    if (pipeline->skeleton_entities == NULL || pipeline->palette_indices == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    if (entity.index >= pipeline->skeleton_capacity) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }

    uint32_t idx = pipeline->palette_indices[entity.index];
    if (idx == SKINNING_PIPELINE_INVALID_INDEX || idx >= pipeline->skeleton_count) {
        return SKINNING_PIPELINE_ERR_NOT_FOUND;
    }
    if (pipeline->skeleton_entities[idx].generation != entity.generation ||
        pipeline->skeleton_entities[idx].index != entity.index) {
        return SKINNING_PIPELINE_ERR_NOT_FOUND;
    }
    *out_index = idx;
    return SKINNING_PIPELINE_OK;
}
