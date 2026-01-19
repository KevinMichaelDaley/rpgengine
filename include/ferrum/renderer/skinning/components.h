#ifndef FERRUM_RENDERER_SKINNING_COMPONENTS_H
#define FERRUM_RENDERER_SKINNING_COMPONENTS_H

#include <stddef.h>
#include <stdint.h>

#include "ferrum/ecs/entity.h"

/** @file
 * @brief ECS components for skeletons and skins.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Sentinel for skeleton root parent. */
#define SKINNING_SKELETON_NO_PARENT UINT32_MAX

/** Skeleton component with local transforms and parent indices. */
typedef struct skinning_skeleton {
    uint32_t joint_count;
    const struct mat4 *local_matrices;
    const uint32_t *parent_indices;
} skinning_skeleton_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_SKINNING_COMPONENTS_H */
