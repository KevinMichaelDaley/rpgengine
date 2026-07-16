/**
 * @file gi_probe_gpu.h
 * @brief GPU probe update: a compute shader marches the RESIDENT combined SDF
 *        (paged baked SDF chunks + dynamic collider boxes) from every probe to
 *        every dynamic light and writes each probe's SH9 (rpg-p3w3, GPU).
 *
 * One compute invocation per probe (parallel over probes). Per probe x light:
 * incident dir + range/spot falloff, a soft sphere-march of the combined SDF for
 * penumbra visibility, project radiance*visibility into the probe's SH9 (matches
 * lm_sh9). The probe SH buffer is exposed as a texture buffer so the forward+
 * material can sample it (@ref gi_probe_gpu_sh_tbo).
 *
 * Needs a GL 4.3 context (compute + SSBO). Manually loads glDispatchCompute /
 * glMemoryBarrier via the gl_loader; everything else is the glad global GL. Owns
 * its program + buffers; frees them in @ref gi_probe_gpu_destroy.
 */
#ifndef FERRUM_RENDERER_GI_GI_PROBE_GPU_H
#define FERRUM_RENDERER_GI_GI_PROBE_GPU_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/renderer/gl_loader.h"
#include "ferrum/renderer/gi/gi_sdf_stream.h"
#include "ferrum/renderer/gi/gi_probe_kernel.h"  /* gi_light_t */
#include "ferrum/renderer/gi/gi_sdf.h"           /* gi_collider_t (boxes) */

#ifdef __cplusplus
extern "C" {
#endif

/** GPU probe-update state. */
typedef struct gi_probe_gpu {
    unsigned int prog;         /**< compute program. */
    unsigned int b_pos, b_sh;  /**< probe position + SH SSBOs. */
    unsigned int b_lights, b_boxes; /**< dynamic light + box SSBOs. */
    unsigned int tbo_sh, tbo_sh_tex; /**< SH buffer texture (for the forward+ sampler). */
    unsigned int tbo_pos_tex;  /**< probe-position buffer texture (for the sampler). */
    uint32_t     n_probes;
    uint32_t     max_lights, max_boxes;
    void (*DispatchCompute)(unsigned int, unsigned int, unsigned int);
    void (*MemoryBarrier)(unsigned int);
    bool ready;
} gi_probe_gpu_t;

/**
 * @brief Create the compute program + buffers sized for @p max_probes /
 *        @p max_lights / @p max_boxes. @p loader supplies the 4.3 compute
 *        entry points. Returns false on failure (no GL 4.3, compile error, ...).
 */
bool gi_probe_gpu_init(gi_probe_gpu_t *g, const gl_loader_t *loader,
                       uint32_t max_probes, uint32_t max_lights,
                       uint32_t max_boxes);

/** @brief Upload probe positions (3 floats/probe) and set the live probe count. */
void gi_probe_gpu_set_probes(gi_probe_gpu_t *g, const float *pos, uint32_t n);

/**
 * @brief Dispatch the update: bind the resident SDF chunks from @p sdf, upload
 *        @p lights (@p n_lights) and dynamic @p boxes (@p n_boxes, sphere/box/
 *        capsule -> only box/sphere folded here) and march every probe to every
 *        light. @p soft_k sets penumbra sharpness. Barriers so the SH TBO is
 *        ready for the forward+ pass.
 */
void gi_probe_gpu_dispatch(gi_probe_gpu_t *g, const gi_sdf_stream_t *sdf,
                           const gi_light_t *lights, uint32_t n_lights,
                           const gi_collider_t *boxes, uint32_t n_boxes,
                           float soft_k);

/** @brief The probe-SH texture buffer (samplerBuffer, R32F, 27/probe). */
unsigned int gi_probe_gpu_sh_tbo(const gi_probe_gpu_t *g);
/** @brief The probe-position texture buffer (samplerBuffer, RGBA32F). */
unsigned int gi_probe_gpu_pos_tbo(const gi_probe_gpu_t *g);

/** @brief Free GL resources. NULL-safe. */
void gi_probe_gpu_destroy(gi_probe_gpu_t *g);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_GPU_H */
