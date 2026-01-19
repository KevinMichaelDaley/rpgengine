#include "ferrum/renderer/skinning/pipeline.h"

#include <stdlib.h>
#include <string.h>

#define SKINNING_PIPELINE_INVALID_INDEX UINT32_MAX

int skinning_pipeline_init(skinning_pipeline_t *pipeline,
                           uint32_t skeleton_capacity,
                           uint32_t max_joints) {
    if (pipeline == NULL || skeleton_capacity == 0u || max_joints == 0u) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    memset(pipeline, 0, sizeof(*pipeline));

    pipeline->skeleton_entities = (entity_t *)calloc(skeleton_capacity, sizeof(entity_t));
    pipeline->palette_indices = (uint32_t *)calloc(skeleton_capacity, sizeof(uint32_t));
    pipeline->palette_matrices = (mat4_t *)calloc((size_t)skeleton_capacity * max_joints, sizeof(mat4_t));
    pipeline->joint_counts = (uint32_t *)calloc(skeleton_capacity, sizeof(uint32_t));
    if (pipeline->skeleton_entities == NULL || pipeline->palette_indices == NULL ||
        pipeline->palette_matrices == NULL || pipeline->joint_counts == NULL) {
        skinning_pipeline_destroy(pipeline);
        return SKINNING_PIPELINE_ERR_OOM;
    }

    for (uint32_t i = 0; i < skeleton_capacity; ++i) {
        pipeline->palette_indices[i] = SKINNING_PIPELINE_INVALID_INDEX;
    }

    pipeline->skeleton_capacity = skeleton_capacity;
    pipeline->max_joints = max_joints;
    return SKINNING_PIPELINE_OK;
}

void skinning_pipeline_destroy(skinning_pipeline_t *pipeline) {
    if (pipeline == NULL) {
        return;
    }
    free(pipeline->skeleton_entities);
    free(pipeline->palette_indices);
    free(pipeline->palette_matrices);
    free(pipeline->joint_counts);
    pipeline->skeleton_entities = NULL;
    pipeline->palette_indices = NULL;
    pipeline->palette_matrices = NULL;
    pipeline->joint_counts = NULL;
    pipeline->skeleton_count = 0u;
    pipeline->skeleton_capacity = 0u;
    pipeline->max_joints = 0u;
}
