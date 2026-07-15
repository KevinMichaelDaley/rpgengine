/**
 * @file lm_visibility.h
 * @brief Ray/segment visibility against the runtime SVO for the offline
 *        lightmap baker.
 *
 * The engine's sparse voxel octree (@ref npc_svo_grid_t) exposes only point
 * queries -- there is no ray API -- so these helpers 3D-DDA-march the finest
 * voxel grid, testing each cell with @ref npc_svo_query_point. This is the
 * visibility primitive every baker pass builds on: shadow rays for direct
 * lighting, gather rays for the bounce solve, and distant-reflector lookups
 * (the hit voxel's material id is returned so the caller can shade it from a
 * material -> albedo/emissive table).
 *
 * Ownership: none -- the caller owns the grid and the out-hit. Nullability:
 * @p svo must be non-NULL; @p out may be NULL in @ref lm_visibility_trace to
 * discard the hit. Errors: a zero-length direction, a grid with voxel_size
 * <= 0, or an origin whose ray misses the grid bounds all report "not
 * occluded" / "no hit" rather than failing. Side effects: none (read-only).
 */
#ifndef FERRUM_LIGHTMAP_LM_VISIBILITY_H
#define FERRUM_LIGHTMAP_LM_VISIBILITY_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/math/vec3.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Result of a ray/segment trace against the SVO. */
typedef struct lm_ray_hit {
    float    t;        /**< Distance along the ray to the hit (metres). */
    vec3_t   position; /**< World-space hit position. */
    vec3_t   normal;   /**< Axis-aligned outward normal of the crossed voxel face. */
    uint16_t material; /**< Material id of the hit voxel (0 if none). */
    bool     hit;      /**< True if a SOLID voxel was entered within maxdist. */
    uint32_t node;     /**< SVO leaf node index of the hit (for the mip pyramid;
                            NPC_SVO_INVALID_NODE if no hit). */
} lm_ray_hit_t;

/**
 * @brief True if the segment origin -> origin + normalize(dir)*maxdist crosses
 *        any SOLID voxel (a shadow-ray occlusion test).
 */
bool lm_visibility_occluded(const npc_svo_grid_t *svo, vec3_t origin,
                            vec3_t dir, float maxdist);

/**
 * @brief Trace to the first SOLID voxel along the ray at distance >= @p tmin.
 *        Fills *out (if non-NULL) with the hit and returns whether anything was
 *        hit within maxdist. @p tmin skips self-hits just off the origin surface
 *        (pass 0 for an unbiased trace).
 */
bool lm_visibility_trace(const npc_svo_grid_t *svo, vec3_t origin,
                         vec3_t dir, float tmin, float maxdist,
                         lm_ray_hit_t *out);

/**
 * @brief True if the two points are mutually visible (the connecting segment is
 *        unoccluded). Both endpoints are biased half a voxel inward so a point
 *        resting on a surface does not shadow itself.
 */
bool lm_visibility_segment(const npc_svo_grid_t *svo, vec3_t p1, vec3_t p2);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_VISIBILITY_H */
