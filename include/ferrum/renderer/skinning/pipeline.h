#ifndef FERRUM_RENDERER_SKINNING_PIPELINE_H
#define FERRUM_RENDERER_SKINNING_PIPELINE_H

#include <stdint.h>

#include "ferrum/ecs/entity.h"
#include "ferrum/ecs/sparse_set.h"
#include "ferrum/job/system.h"
#include "ferrum/math/mat4.h"
#include "ferrum/renderer/bone_palette.h"
#include "ferrum/renderer/skinning/components.h"
#include "ferrum/renderer/skinning/skin.h"

/** @file
 * @brief Skinning pipeline evaluation and palette uploads.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Status codes for skinning pipeline. */
#define SKINNING_PIPELINE_OK 0
#define SKINNING_PIPELINE_ERR_INVALID 1
#define SKINNING_PIPELINE_ERR_OOM 2
#define SKINNING_PIPELINE_ERR_NOT_FOUND 3

/** Draw list ordering for skinning renders. */
typedef struct skinning_draw_list {
    entity_t *entities;
    uint32_t count;
} skinning_draw_list_t;

/** Skinning pipeline state. */
typedef struct skinning_pipeline {
    entity_t *skeleton_entities;
    uint32_t *palette_indices;
    mat4_t *palette_matrices;
    uint32_t *joint_counts;
    void *job_contexts;
    uint32_t *draw_list_palette_indices;
    uint32_t skeleton_count;
    uint32_t skeleton_capacity;
    uint32_t max_joints;
} skinning_pipeline_t;

/**
 * @brief Initialize pipeline storage.
 * @param pipeline Pipeline output.
 * @param skeleton_capacity Maximum skeletons supported.
 * @param max_joints Maximum joints per skeleton.
 * @return Status code.
 */
int skinning_pipeline_init(skinning_pipeline_t *pipeline,
                           uint32_t skeleton_capacity,
                           uint32_t max_joints);

/**
 * @brief Release pipeline storage.
 * @param pipeline Pipeline pointer.
 */
void skinning_pipeline_destroy(skinning_pipeline_t *pipeline);

/**
 * @brief Evaluate skeletons and build palette mapping.
 * @param pipeline Pipeline pointer.
 * @param system Job system used for evaluation.
 * @param skeletons Skeleton sparse set base.
 * @param skins Skin sparse set base.
 * @return Status code.
 */
int skinning_pipeline_update(skinning_pipeline_t *pipeline,
                             job_system_t *system,
                             ecs_sparse_set_base_t *skeletons,
                             ecs_sparse_set_base_t *skins);

/**
 * @brief Query palette index for an entity.
 * @param pipeline Pipeline pointer.
 * @param entity Entity handle.
 * @param out_index Output palette index.
 * @return Status code.
 */
int skinning_pipeline_palette_index(const skinning_pipeline_t *pipeline,
                                   entity_t entity,
                                   uint32_t *out_index);

/**
 * @brief Upload palette for a given palette index.
 * @param pipeline Pipeline pointer.
 * @param palette Palette buffer.
 * @param palette_index Palette index.
 * @return Status code.
 */
int skinning_pipeline_upload_palette(const skinning_pipeline_t *pipeline,
                                    bone_palette_buffer_t *palette,
                                    uint32_t palette_index);

/**
 * @brief Build a deterministic draw list ordered by palette index.
 * @param pipeline Pipeline pointer.
 * @param skins Skin sparse set base.
 * @param out_list Output draw list.
 * @param storage Storage for entity list.
 * @param storage_capacity Capacity of storage buffer.
 * @return Status code.
 */
int skinning_pipeline_build_draw_list(const skinning_pipeline_t *pipeline,
                                      ecs_sparse_set_base_t *skins,
                                      skinning_draw_list_t *out_list,
                                      entity_t *storage,
                                      uint32_t storage_capacity);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_SKINNING_PIPELINE_H */
