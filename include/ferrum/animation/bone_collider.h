/**
 * @file bone_collider.h
 * @brief Per-bone collision shape descriptor for skeletal physics.
 *
 * Each bone in a skeleton may optionally carry a collision shape
 * (capsule, box, sphere, or convex hull) used by the ragdoll builder
 * and the animated-body physics pipeline.
 *
 * Exported from Blender as part of the fskel v2 COLL chunk.
 *
 * Public types: 2 (bone_collider_shape_t, bone_collider_desc_t)
 */

#ifndef FERRUM_ANIMATION_BONE_COLLIDER_H
#define FERRUM_ANIMATION_BONE_COLLIDER_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Collision shape type for a bone.
 */
typedef enum bone_collider_shape {
    BONE_COLLIDER_NONE         = 0, /**< No collision geometry. */
    BONE_COLLIDER_CAPSULE      = 1, /**< Capsule: params = {radius, height, axis}. */
    BONE_COLLIDER_BOX          = 2, /**< Box: params = {half_x, half_y, half_z}. */
    BONE_COLLIDER_SPHERE       = 3, /**< Sphere: params = {radius, 0, 0}. */
    BONE_COLLIDER_CONVEX_HULL  = 4, /**< Convex hull: vertex data referenced by offset+count. */
} bone_collider_shape_t;

/**
 * @brief Per-bone collision shape descriptor.
 *
 * Stored in the fskel v2 COLL chunk, one per joint.
 * Convex hull vertex data is stored separately and referenced
 * by hull_offset (byte offset) and hull_count (vertex count).
 *
 * @par Ownership
 * This is a plain data struct with no internal allocations.
 * Hull vertex data is owned by the skeleton_def_t.
 */
typedef struct bone_collider_desc {
    uint32_t shape_type;     /**< bone_collider_shape_t discriminator. */
    float    params[6];      /**< Shape parameters:
                              *   capsule: {radius, height, axis_idx(0=X,1=Y,2=Z), 0,0,0}
                              *   box:     {half_x, half_y, half_z, 0, 0, 0}
                              *   sphere:  {radius, 0, 0, 0, 0, 0}
                              *   hull:    unused (data in hull_vertices). */
    uint32_t ccd_enabled;    /**< 1 = enable CCD for this bone's body. */
    uint32_t is_kinematic;   /**< 1 = skip Euler-Verlet (animation-only bone). */
    float    mass;           /**< Mass override (0 = auto from volume × density).
                              *   Ignored if is_kinematic. */
    uint32_t hull_offset;    /**< Byte offset into hull vertex data (shape_type=4 only). */
    uint32_t hull_count;     /**< Vertex count for convex hull (shape_type=4 only). */
} bone_collider_desc_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_BONE_COLLIDER_H */
