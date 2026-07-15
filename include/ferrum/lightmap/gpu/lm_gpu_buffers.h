/**
 * @file lm_gpu_buffers.h
 * @brief std430-ready GPU records for the SVO nodes + luxels of the compute
 *        gather. Fixed-size, 16-byte-aligned so they map 1:1 onto SSBO structs.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_BUFFERS_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_BUFFERS_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** One SVO node for the gather: child indices + the prefiltered shading pyramid
 *  (avg subtree diffuse reflectance + emissive radiance a distant cone samples).
 *  64 bytes: uint[8] children, then two padded vec4s. */
typedef struct lm_gpu_node {
    uint32_t children[8];  /**< child indices or NPC_SVO_INVALID_NODE. */
    float    diffuse[4];   /**< rgb + pad. */
    float    emissive[4];  /**< rgb + pad. */
} lm_gpu_node_t;

/** One luxel: world position + unit normal (padded vec4s). 32 bytes. */
typedef struct lm_gpu_luxel {
    float pos[4];    /**< xyz + pad. */
    float normal[4]; /**< xyz + pad. */
} lm_gpu_luxel_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_BUFFERS_H */
