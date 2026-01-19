#ifndef FERRUM_RENDERER_SKINNING_SKIN_H
#define FERRUM_RENDERER_SKINNING_SKIN_H

#include <stdint.h>

#include "ferrum/ecs/entity.h"

/** @file
 * @brief ECS skin component referencing a skeleton.
 */

#ifdef __cplusplus
extern "C" {
#endif

/** Skin component referencing a skeleton entity. */
typedef struct skinning_skin {
    entity_t skeleton_entity;
} skinning_skin_t;

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_SKINNING_SKIN_H */
