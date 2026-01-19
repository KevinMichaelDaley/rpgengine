#include "ferrum/renderer/skinning/pipeline.h"

#include <stdlib.h>
#include <string.h>

#define SKINNING_PIPELINE_INVALID_INDEX UINT32_MAX

struct skinning_job_context {
    const skinning_skeleton_t *skeleton;
    mat4_t *output;
    uint32_t max_joints;
};

static void skinning_eval_job(void *user_data) {
    struct skinning_job_context *ctx = (struct skinning_job_context *)user_data;
    const skinning_skeleton_t *skeleton = ctx->skeleton;
    mat4_t *output = ctx->output;
    uint32_t joint_count = skeleton->joint_count;
    for (uint32_t i = 0; i < joint_count; ++i) {
        uint32_t parent = skeleton->parent_indices[i];
        if (parent == SKINNING_SKELETON_NO_PARENT) {
            output[i] = skeleton->local_matrices[i];
        } else {
            output[i] = mat4_mul(output[parent], skeleton->local_matrices[i]);
        }
    }
    for (uint32_t i = joint_count; i < ctx->max_joints; ++i) {
        output[i] = mat4_identity();
    }
}

static int skinning_joint_is_sorted(const uint32_t *parents, uint32_t joint_count) {
    for (uint32_t i = 0; i < joint_count; ++i) {
        uint32_t parent = parents[i];
        if (parent != SKINNING_SKELETON_NO_PARENT && parent >= i) {
            return 0;
        }
    }
    return 1;
}

static int skinning_collect_skeletons(skinning_pipeline_t *pipeline,
                                     ecs_sparse_set_base_t *skeletons,
                                     ecs_sparse_set_base_t *skins,
                                     uint32_t *out_count) {
    uint32_t skeleton_count = 0;
    if (skins->size > pipeline->skeleton_capacity) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }

    for (uint32_t i = 0; i < pipeline->skeleton_capacity; ++i) {
        pipeline->palette_indices[i] = SKINNING_PIPELINE_INVALID_INDEX;
        pipeline->joint_counts[i] = 0u;
    }

    skinning_skin_t *skin_data = (skinning_skin_t *)skins->dense;
    for (uint32_t i = 0; i < skins->size; ++i) {
        entity_t skeleton_entity = skin_data[i].skeleton_entity;
        if (skeleton_entity.index >= pipeline->skeleton_capacity) {
            return SKINNING_PIPELINE_ERR_INVALID;
        }
        if (ecs_sparse_set_base_get(skeletons, skeleton_entity) == NULL) {
            return SKINNING_PIPELINE_ERR_NOT_FOUND;
        }
        int exists = 0;
        for (uint32_t j = 0; j < skeleton_count; ++j) {
            if (pipeline->skeleton_entities[j].index == skeleton_entity.index &&
                pipeline->skeleton_entities[j].generation == skeleton_entity.generation) {
                exists = 1;
                break;
            }
        }
        if (!exists) {
            if (skeleton_count >= pipeline->skeleton_capacity) {
                return SKINNING_PIPELINE_ERR_INVALID;
            }
            pipeline->skeleton_entities[skeleton_count++] = skeleton_entity;
        }
    }

    for (uint32_t i = 1; i < skeleton_count; ++i) {
        entity_t key = pipeline->skeleton_entities[i];
        uint32_t j = i;
        while (j > 0 && pipeline->skeleton_entities[j - 1].index > key.index) {
            pipeline->skeleton_entities[j] = pipeline->skeleton_entities[j - 1];
            --j;
        }
        pipeline->skeleton_entities[j] = key;
    }

    for (uint32_t i = 0; i < skeleton_count; ++i) {
        pipeline->palette_indices[pipeline->skeleton_entities[i].index] = i;
    }

    *out_count = skeleton_count;
    return SKINNING_PIPELINE_OK;
}

int skinning_pipeline_update(skinning_pipeline_t *pipeline,
                             job_system_t *system,
                             ecs_sparse_set_base_t *skeletons,
                             ecs_sparse_set_base_t *skins) {
    if (pipeline == NULL || system == NULL || skeletons == NULL || skins == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }
    if (pipeline->skeleton_entities == NULL || pipeline->palette_indices == NULL ||
        pipeline->palette_matrices == NULL || pipeline->joint_counts == NULL) {
        return SKINNING_PIPELINE_ERR_INVALID;
    }

    uint32_t skeleton_count = 0;
    int status = skinning_collect_skeletons(pipeline, skeletons, skins, &skeleton_count);
    if (status != SKINNING_PIPELINE_OK) {
        pipeline->skeleton_count = 0u;
        return status;
    }

    pipeline->skeleton_count = skeleton_count;
    if (skeleton_count == 0u) {
        return SKINNING_PIPELINE_OK;
    }

    struct skinning_job_context *contexts =
        (struct skinning_job_context *)malloc(sizeof(struct skinning_job_context) * skeleton_count);
    if (contexts == NULL) {
        pipeline->skeleton_count = 0u;
        return SKINNING_PIPELINE_ERR_OOM;
    }

    for (uint32_t i = 0; i < skeleton_count; ++i) {
        entity_t skeleton_entity = pipeline->skeleton_entities[i];
        skinning_skeleton_t *skeleton =
            (skinning_skeleton_t *)ecs_sparse_set_base_get(skeletons, skeleton_entity);
        if (skeleton == NULL || skeleton->joint_count == 0u || skeleton->joint_count > pipeline->max_joints ||
            skeleton->local_matrices == NULL || skeleton->parent_indices == NULL) {
            free(contexts);
            pipeline->skeleton_count = 0u;
            return SKINNING_PIPELINE_ERR_INVALID;
        }
        if (!skinning_joint_is_sorted(skeleton->parent_indices, skeleton->joint_count)) {
            free(contexts);
            pipeline->skeleton_count = 0u;
            return SKINNING_PIPELINE_ERR_INVALID;
        }
        pipeline->joint_counts[i] = skeleton->joint_count;
        contexts[i].skeleton = skeleton;
        contexts[i].output = pipeline->palette_matrices + ((size_t)i * pipeline->max_joints);
        contexts[i].max_joints = pipeline->max_joints;
        if (job_dispatch(system, skinning_eval_job, &contexts[i], 0, NULL) == JOB_ID_INVALID) {
            free(contexts);
            pipeline->skeleton_count = 0u;
            return SKINNING_PIPELINE_ERR_INVALID;
        }
    }

    if (job_system_wait_idle(system) != 0) {
        free(contexts);
        pipeline->skeleton_count = 0u;
        return SKINNING_PIPELINE_ERR_INVALID;
    }

    free(contexts);
    return SKINNING_PIPELINE_OK;
}
