#include "ferrum/renderer/skinning/pipeline.h"

int skinning_pipeline_upload_palette(const skinning_pipeline_t *pipeline,
                                    bone_palette_buffer_t *palette,
                                    uint32_t palette_index) {
    if (pipeline == NULL || palette == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    if (pipeline->palette_matrices == NULL || pipeline->joint_counts == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    if (palette_index >= pipeline->skeleton_count) {
        return SKINNING_PIPELINE_ERR_NOT_FOUND;
    }

    uint32_t joint_count = pipeline->joint_counts[palette_index];
    if (joint_count == 0u) {
        return SKINNING_PIPELINE_ERR_NOT_FOUND;
    }
    if (palette->max_bones < joint_count) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }

    size_t size = (size_t)joint_count * sizeof(mat4_t);
    mat4_t *data = pipeline->palette_matrices + ((size_t)palette_index * pipeline->max_joints);
    if (bone_palette_buffer_update(palette, data, size) != BONE_PALETTE_OK) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    return SKINNING_PIPELINE_OK;
}
