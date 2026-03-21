/**
 * @file bone_pick.h
 * @brief Ray-capsule intersection and bone picking for skeleton editing.
 *
 * Provides ray-capsule intersection testing and a nearest-bone picker
 * used by the viewport input system to select individual bones.
 *
 * Public types: bone_pick_candidate_t (1 / 2-type rule).
 */
#ifndef FERRUM_EDITOR_VIEWPORT_BONE_PICK_H
#define FERRUM_EDITOR_VIEWPORT_BONE_PICK_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/math/vec3.h"

/* Forward declarations. */
struct editor_ray;

/**
 * @brief A candidate bone for raycast picking.
 */
typedef struct bone_pick_candidate {
    uint32_t bone_index; /**< Bone index within the skeleton. */
    vec3_t   cap_a;      /**< Capsule endpoint A (world space). */
    vec3_t   cap_b;      /**< Capsule endpoint B (world space). */
    float    radius;     /**< Capsule radius. */
} bone_pick_candidate_t;

/**
 * @brief Test ray-capsule intersection.
 *
 * A capsule is defined by two endpoints (cap_a, cap_b) and a radius.
 * The test checks the infinite cylinder between the endpoints, then
 * clamps to the finite segment, and tests the two spherical caps.
 *
 * @param ray     Ray (non-NULL).
 * @param cap_a   Capsule endpoint A.
 * @param cap_b   Capsule endpoint B.
 * @param radius  Capsule radius (>= 0).
 * @param t_hit   Output: distance along ray to nearest hit (non-NULL).
 * @return true if ray intersects the capsule.
 */
bool ray_intersect_capsule(const struct editor_ray *ray,
                            vec3_t cap_a, vec3_t cap_b,
                            float radius, float *t_hit);

/**
 * @brief Find the nearest bone hit by a ray.
 *
 * Tests the ray against each candidate bone capsule and returns the
 * bone index of the closest hit.
 *
 * @param ray          Ray (non-NULL).
 * @param candidates   Array of bone pick candidates (may be NULL if count is 0).
 * @param count        Number of candidates.
 * @param out_bone     Output: bone index of nearest hit (non-NULL).
 * @return true if any bone was hit.
 */
bool pick_nearest_bone(const struct editor_ray *ray,
                        const bone_pick_candidate_t *candidates,
                        uint32_t count, uint32_t *out_bone);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_VIEWPORT_BONE_PICK_H */
