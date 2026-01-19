#ifndef FERRUM_RENDERER_SKINNING_PIPELINE_INTERNAL_H
#define FERRUM_RENDERER_SKINNING_PIPELINE_INTERNAL_H

#include "ferrum/math/mat4.h"
#include "ferrum/renderer/skinning/components.h"

struct skinning_job_context {
    const skinning_skeleton_t *skeleton;
    mat4_t *output;
    uint32_t max_joints;
};

#endif /* FERRUM_RENDERER_SKINNING_PIPELINE_INTERNAL_H */
