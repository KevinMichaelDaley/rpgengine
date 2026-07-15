/**
 * @file lm_gpu_scene.h
 * @brief std430-ready GPU records for the analytic lights + the gather's global
 *        parameter block.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_SCENE_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_SCENE_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One analytic light. Scalars ride in the vec4 padding: position.w = kind,
 *  direction.w = range, color.w = cos_inner, cone.x = cos_outer. 64 bytes. */
typedef struct lm_gpu_light {
    float position[4];  /**< xyz world position, w = kind. */
    float direction[4]; /**< xyz emission dir, w = range. */
    float color[4];     /**< rgb intensity/irradiance, w = cos_inner. */
    float cone[4];      /**< x = cos_outer, yzw pad. */
} lm_gpu_light_t;

/** Global gather parameters (a small uniform/SSBO). 64 bytes. */
typedef struct lm_gpu_params {
    float    bounds_min[4]; /**< world min xyz, w = voxel_size. */
    float    bounds_max[4]; /**< world max xyz, w = transition. */
    float    misc[4];       /**< x = maxdist, yzw reserved. */
    uint32_t counts[4];     /**< n_nodes, n_luxels, n_lights, bounces. */
} lm_gpu_params_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_SCENE_H */
