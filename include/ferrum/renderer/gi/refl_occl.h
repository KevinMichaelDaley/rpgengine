/**
 * @file refl_occl.h
 * @brief Specular-occlusion cone march over the baked SDF (rpg-akwc): the
 *        atlas alpha channel. Soft-shadow style visibility -- the minimum of
 *        clamp(d / (cone_tan * t)) along the march, so wider cones (higher
 *        roughness mips) see more occlusion. Pure CPU, no allocation.
 */
#ifndef FERRUM_RENDERER_GI_REFL_OCCL_H
#define FERRUM_RENDERER_GI_REFL_OCCL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Signed-distance sample callback (matches probe_chunk_sdf_sample). */
typedef float (*refl_sdf_fn)(const float p[3], void *user);

/**
 * Cone visibility using sample callback @p fn (NULL fn returns 1). Same
 * semantics as @ref refl_occl_cone; @p step_floor is the minimum march
 * step in metres (pass the SDF voxel size; <= 0 uses 0.05).
 */
float refl_occl_cone_fn(refl_sdf_fn fn, void *user, const float p[3],
                        const float dir[3], float cone_tan, float max_dist);

/**
 * Cone visibility from @p p along unit @p dir against the baked SDF
 * (@ref gi_sdf_baked_sample layout). @p cone_tan is the cone half-angle
 * tangent (>= ~0.02), @p max_dist the march limit in metres. Returns 0..1
 * (1 = unoccluded). NULL @p dist (no field) returns 1.
 */
float refl_occl_cone(const float *dist, const int32_t dims[3],
                     const float origin[3], float voxel, const float p[3],
                     const float dir[3], float cone_tan, float max_dist);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_REFL_OCCL_H */
