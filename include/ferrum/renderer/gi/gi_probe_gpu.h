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

/**
 * @brief All probe-GI tuning in one value type (rpg-2vfm).
 *
 * These were previously env-only (GI_*), so the client render path ran them blind
 * at their defaults and a level could not carry its own look. They now travel
 * render_config (JSON) -> render_world_config -> gi_runtime_config -> here. The
 * GI_* env vars are still honoured as a LIVE-TUNING override on top, but the
 * config is the source of truth. Zero-initialising this struct is not valid --
 * use gi_probe_tuning_defaults().
 */
typedef struct gi_probe_tuning {
    int   field_on;      /**< DDGI recurrent gather (0 = pure per-ray SDF march). */
    int   mis;           /**< MIS-importance-sampled march directions. */
    int   hybrid;        /**< field bounce + hero SDF marches. */
    int   hero;          /**< hero SDF marches per probe (0..4). */
    int   samples;       /**< source-probe samples per gather (>= source count => exact). */
    int   spec_lobes;    /**< SG specular lobes summed per probe (0..3). */
    int   update_interval;/**< re-trace cadence in frames. */
    int   n_probe_groups;/**< staggered dithered probe groups (1 = no stagger). */
    float bounce;        /**< per-bounce transport gain; steady state = 1/(1-bounce). */
    float near_dist;     /**< direct-sample vs stochastic-gather threshold (m). */
    float dmax;          /**< nearest-surface distance for a probe to be a SOURCE. */
    float emin;          /**< emission luminance over which a probe is a SOURCE. */
    float norm_gate;     /**< |sdf| under which a probe is a SURFACE probe (has a normal). */
    float stat_scale;    /**< scale on the probes' STATIC bounce gather. */
    float smooth;        /**< steady-state probe temporal-EMA blend. */
    float vis_bias;      /**< Chebyshev self-visible band (m) -- probe-lattice dot artifacts. */
    float vis_varmin;    /**< Chebyshev variance floor: larger = softer falloff. */
    float vis_sharp;     /**< Chebyshev falloff exponent: 1 = soft, 2 = sharp. */
} gi_probe_tuning_t;

/** @brief Fill @p t with the engine defaults (the values the GI_* envs defaulted to). */
void gi_probe_tuning_defaults(gi_probe_tuning_t *t);

/** GPU probe-update state. */
typedef struct gi_probe_gpu {
    unsigned int prog;         /**< compute program. */
    /* Cached uniform locations (queried ONCE at init). The staggered path
     * dispatches every frame, so per-dispatch glGetUniformLocation string lookups
     * (~55 of them) add up; cache them. sdf_* arrays are sized GI_SDF_MAX_RESIDENT
     * (8). */
    struct { int nprobes, nlights, nboxes, soft, ncones, albedo, temporal;
             int ngroups, group, grid_dim, grid_origin, grid_cell;
             int field_on, near_dist, static_on, static_k, static_irr;
             int pass, seed, dmax, emin, nsamp, bounce;
             int mis, norm_gate, hybrid, hero, stat_scale;
             int dyn_alb, dyn_origin, dyn_dim, dyn_vox, dyn_on;
             int static_origin, static_dim, static_vox;
             int sdf_active[8], sdf[8], sdf_origin[8], sdf_dim[8], sdf_vox[8]; } loc;
    unsigned int b_pos, b_sh;  /**< probe position + SH SSBOs. */
    unsigned int b_lights, b_boxes; /**< dynamic light + box SSBOs. */
    unsigned int b_depth;      /**< DDGI octahedral depth SSBO (mean, meanSq / texel). */
    unsigned int b_sg;         /**< SG specular lobe SSBO (8 floats/probe). */
    unsigned int b_active;     /**< stochastic-radiosity scratch: [count][indices..]. */
    unsigned int b_emit;       /**< per-probe direct-injection SH (24 floats/probe). */
    unsigned int b_norm;       /**< per-probe surface normal (vec4: xyz + validity). */
    float       *pos_shadow;   /**< CPU shadow of the packed probe vec4s [max_probes*4]
                                *   (xyz + active flag). Lets the ACTIVE mask be
                                *   rewritten without a glGetBufferSubData read-back
                                *   (a GPU->CPU sync) on every residency change. */
    uint32_t     pos_cap;      /**< probe capacity of @c pos_shadow. */
    gi_probe_tuning_t tuning;  /**< probe-GI tuning (config-driven; GI_* env overrides). */
    unsigned int dyn_tex;      /**< sparse DYNAMIC albedo volume (RGBA8 3D, 0 = none). */
    int          dyn_dim[3];   /**< dynamic-volume voxel dims. */
    float        dyn_origin[3];/**< dynamic-volume world origin (min corner). */
    float        dyn_vox;      /**< dynamic-volume voxel size (m). */
    int          dyn_on;       /**< 1 = the volume has dynamic coverage this update. */
    unsigned int tbo_sh, tbo_sh_tex; /**< SH buffer texture (for the forward+ sampler). */
    unsigned int tbo_pos_tex;  /**< probe-position buffer texture (for the sampler). */
    unsigned int tbo_depth_tex; /**< depth buffer texture (RG32F: mean, meanSq). */
    unsigned int depth_arr;    /**< RG32F 2D-array (8x8 octahedral/probe, one layer
                                *   per probe) the compute mirrors the depth into, so
                                *   the forward+ samples it with HARDWARE bilinear
                                *   (GL_LINEAR) -- one tap vs 4 texelFetch + manual mix. */
    unsigned int tbo_sg_tex;   /**< SG lobe texture (RGBA32F: axis+kappa, rgb+pad). */
    uint32_t     n_probes;
    uint32_t     max_lights, max_boxes;
    /* Static irradiance volume (rpg-pau4): baked-lightmap E on a coarse world
     * grid, gathered by the cone trace so probes carry the static ambience. */
    unsigned int static_tex;          /**< GL sampler3D (0 = none). */
    float        static_origin[3];    /**< world-space grid origin (min corner). */
    float        static_dim[3];       /**< grid dims in cells (x,y,z). */
    float        static_vox;          /**< cell size (metres). */
    float        static_k;            /**< boost applied to the gathered E. */
    void (*DispatchCompute)(unsigned int, unsigned int, unsigned int);
    void (*MemoryBarrier)(unsigned int);
    void (*BindImageTexture)(unsigned int, unsigned int, int, unsigned char,
                             int, unsigned int, unsigned int);
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
 * @brief Bind the static irradiance volume the cone trace gathers (rpg-pau4).
 *        @p tex is a GL sampler3D of RGB irradiance covering the grid at
 *        @p origin with @p dim cells of size @p vox; @p k boosts the gathered E.
 *        @p tex == 0 disables the term. Metadata is copied; the texture is not
 *        owned. NULL-safe.
 */
void gi_probe_gpu_set_static(gi_probe_gpu_t *g, unsigned int tex,
                             const float origin[3], const float dim[3],
                             float vox, float k);

/**
 * @brief (Re)create + CLEAR the sparse DYNAMIC albedo volume covering
 *        @p aabb_min..@p aabb_max at ~@p vox metres/voxel, and return its texture
 *        so the caller can rasterise dynamic geometry into it (gi_voxelize).
 *
 * Dynamic objects are absent from the baked voxel albedo, so without this a probe
 * ray that hits one bounces the neutral grey fallback (occlusion only). Call once
 * per probe update on the GL thread, then voxelise, then gi_probe_gpu_dyn_enable(1).
 * The volume is clamped to a low-res voxel budget (probe scale).
 *
 * @param out_dim,out_extent  filled with the chosen voxel dims + world extent.
 * @return the 3D texture id (0 on failure).
 */
unsigned int gi_probe_gpu_dyn_volume(gi_probe_gpu_t *g, const float aabb_min[3],
                                     const float aabb_max[3], float vox,
                                     int out_dim[3], float out_extent[3]);

/** @brief Set the probe-GI tuning used by the next dispatch (NULL = defaults). */
void gi_probe_gpu_set_tuning(gi_probe_gpu_t *g, const gi_probe_tuning_t *t);

/** @brief Enable/disable the dynamic-albedo term for the next dispatch. */
void gi_probe_gpu_dyn_enable(gi_probe_gpu_t *g, int on);

/**
 * @brief Set the per-probe ACTIVE mask (@p active[@p n], 1 = resident) used by
 *        probe streaming. Inactive probes keep their slot (the forward+ addresses
 *        the grid positionally, so the array must stay dense) and their last
 *        coefficients, but the update skips them. NULL = all active.
 */
void gi_probe_gpu_set_active(gi_probe_gpu_t *g, const unsigned char *active, uint32_t n);

/**
 * @brief Dispatch the update: bind the resident SDF chunks from @p sdf, upload
 *        @p lights (@p n_lights) and dynamic @p boxes (@p n_boxes, sphere/box/
 *        capsule -> only box/sphere folded here) and march every probe to every
 *        light. @p soft_k sets penumbra sharpness. Barriers so the SH TBO is
 *        ready for the forward+ pass.
 *
 * Staggered updates: @p ngroups > 1 splits the probes into that many spatially-
 * dithered groups; only group @p group re-traces this dispatch (the rest keep
 * their previous coefficients). @p ngroups <= 1 traces every probe.
 */
void gi_probe_gpu_dispatch(gi_probe_gpu_t *g, const gi_sdf_stream_t *sdf,
                           const gi_light_t *lights, uint32_t n_lights,
                           const gi_collider_t *boxes, uint32_t n_boxes,
                           float soft_k, float temporal, int ngroups, int group,
                           const int grid_dim[3], const float grid_origin[3],
                           const float grid_cell[3]);

/** @brief The probe-SH texture buffer (samplerBuffer, R32F, 27/probe). */
unsigned int gi_probe_gpu_sh_tbo(const gi_probe_gpu_t *g);
/** @brief The probe-position texture buffer (samplerBuffer, RGBA32F). */
unsigned int gi_probe_gpu_pos_tbo(const gi_probe_gpu_t *g);
/** @brief The DDGI depth texture buffer (samplerBuffer, RG32F: mean, meanSq),
 *         8x8 octahedral texels per probe. 0 if unavailable. */
unsigned int gi_probe_gpu_depth_tbo(const gi_probe_gpu_t *g);
/** @brief The SG specular-lobe texture buffer (samplerBuffer, RGBA32F): 2 texels
 *         per probe (axis+kappa, rgb+pad). 0 if unavailable. */
unsigned int gi_probe_gpu_sg_tbo(const gi_probe_gpu_t *g);

/** @brief Free GL resources. NULL-safe. */
void gi_probe_gpu_destroy(gi_probe_gpu_t *g);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_PROBE_GPU_H */
