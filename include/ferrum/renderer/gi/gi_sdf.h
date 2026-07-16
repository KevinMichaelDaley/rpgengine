/**
 * @file gi_sdf.h
 * @brief Combined dynamic SDF for probe cone-tracing (rpg-d1ok): a baked scene
 *        distance field min-combined with analytic dynamic-collider SDFs.
 *
 * The dynamic-light GI cone-traces the scene at query time. The static geometry
 * comes from the baked per-chunk SDF (rpg-iudw), sampled trilinearly; MOVING
 * bodies are folded in as cheap analytic signed-distance primitives (sphere /
 * box / capsule from physics). The union of solids is the per-point MINIMUM of
 * all the signed distances, so @ref gi_sdf_combined returns
 * min(baked(p), collider_0(p), collider_1(p), ...).
 *
 * The baked field is passed as raw (dist, dims, origin, voxel) so this module
 * does not depend on the lightmap file format. Distances are in metres, negative
 * inside. Sampling outside the baked grid returns a large positive (far / no
 * occlusion) so colliders still contribute. Pure math; no allocation.
 */
#ifndef FERRUM_RENDERER_GI_GI_SDF_H
#define FERRUM_RENDERER_GI_GI_SDF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Analytic collider shape. */
typedef enum gi_collider_kind {
    GI_COLLIDER_SPHERE = 0,  /**< centre @c a, radius @c ext[0]. */
    GI_COLLIDER_BOX = 1,     /**< centre @c a, half-extents @c ext (axis-aligned). */
    GI_COLLIDER_CAPSULE = 2  /**< segment @c a..@c b, radius @c ext[0]. */
} gi_collider_kind_t;

/** One dynamic collider as an analytic SDF primitive. */
typedef struct gi_collider {
    gi_collider_kind_t kind;
    float a[3];   /**< sphere/box centre, or capsule endpoint A. */
    float b[3];   /**< capsule endpoint B (unused otherwise). */
    float ext[3]; /**< box half-extents; sphere/capsule radius in ext[0]. */
} gi_collider_t;

/** @brief Signed distance from @p p to collider @p c (metres, negative inside). */
float gi_collider_distance(const gi_collider_t *c, const float p[3]);

/**
 * @brief Trilinearly sample the baked distance field at world @p p. @p dist is
 *        dims.x*dims.y*dims.z floats (x fastest), the grid min corner is
 *        @p origin, cell edge @p voxel. Returns a large positive (>= 1e30) when
 *        @p p is outside the grid.
 */
float gi_sdf_baked_sample(const float *dist, const int32_t dims[3],
                          const float origin[3], float voxel, const float p[3]);

/**
 * @brief min(baked(@p p), colliders...) -- the combined static+dynamic signed
 *        distance. @p colliders may be NULL when @p n == 0.
 */
float gi_sdf_combined(const float *dist, const int32_t dims[3],
                      const float origin[3], float voxel,
                      const gi_collider_t *colliders, uint32_t n,
                      const float p[3]);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_RENDERER_GI_GI_SDF_H */
