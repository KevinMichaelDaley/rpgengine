/**
 * @file mesh_bone_collision.h
 * @brief Per-bone collision generation from mesh segments.
 *
 * Takes per-bone triangle segments (from mesh_bone_segment.h) and
 * runs convex decomposition on each, producing collision descriptors
 * that can override a skeleton definition's per-bone colliders.
 *
 * Ownership: collision set owns decompose results (heap-allocated).
 * Nullability: all pointer params must be non-NULL unless documented.
 * Error semantics: build returns false on invalid args.
 * Side effects: allocates heap memory for decompose results.
 *
 * Public types: 2 (mesh_bone_collision_t, mesh_bone_collision_set_t).
 */
#ifndef FERRUM_EDITOR_MESH_BONE_COLLISION_H
#define FERRUM_EDITOR_MESH_BONE_COLLISION_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/physics/convex_decompose.h"

/* Forward declarations. */
struct mesh_bone_segments;
struct bone_collider_desc;
struct skeleton_def;

/**
 * @brief Per-bone collision decomposition result.
 */
typedef struct mesh_bone_collision {
    phys_decompose_result_t decomp;  /**< Convex decomposition result. */
    uint32_t bone_index;             /**< Bone this collision belongs to. */
    bool valid;                      /**< True if decomposition succeeded. */
} mesh_bone_collision_t;

/**
 * @brief Set of per-bone collision results.
 */
typedef struct mesh_bone_collision_set {
    mesh_bone_collision_t *entries; /**< Heap-allocated entries (owned). */
    uint32_t count;                /**< Number of entries. */
} mesh_bone_collision_set_t;

/* ---- Build (mesh_bone_collision_build.c) ---- */

/**
 * @brief Build per-bone collision data from mesh segments.
 *
 * For each bone segment with enough triangles (≥ 4), runs
 * phys_decompose_mesh() (V-ACD). Bones with fewer than 4 triangles
 * get a single convex hull via phys_convex_hull_build().
 *
 * @param set       Output collision set (non-NULL).
 * @param segments  Input per-bone triangle segments (non-NULL).
 * @return true on success, false on invalid args.
 *
 * Ownership: allocates entries array. Call mesh_bone_collision_destroy().
 */
bool mesh_bone_collision_build(mesh_bone_collision_set_t *set,
                                const struct mesh_bone_segments *segments);

/**
 * @brief Free all collision data.
 *
 * @param set  Collision set to destroy (non-NULL, safe if already destroyed).
 */
void mesh_bone_collision_destroy(mesh_bone_collision_set_t *set);

/* ---- Apply (mesh_bone_collision_apply.c) ---- */

/**
 * @brief Convert collision set to bone_collider_desc_t array + hull vertices.
 *
 * Builds a flat array of collider descriptors (one per valid bone) and
 * a flat hull vertex buffer matching the format expected by
 * phys_anim_entity_create().
 *
 * @param set            Input collision set (non-NULL).
 * @param out_descs      Output collider descriptors (one per entry, non-NULL).
 * @param out_hull_verts Output hull vertex buffer (x,y,z triples, non-NULL).
 * @param out_vert_count Output: total hull vertices written.
 * @param max_verts      Maximum vertices that fit in out_hull_verts.
 * @return true on success, false if out_hull_verts overflows.
 */
bool mesh_bone_collision_to_collider_descs(
    const mesh_bone_collision_set_t *set,
    struct bone_collider_desc *out_descs,
    float *out_hull_verts,
    uint32_t *out_vert_count,
    uint32_t max_verts);

/**
 * @brief Override a skeleton copy's per-bone colliders with mesh collision.
 *
 * Replaces the skeleton's colliders array and hull_vertices with data
 * from the collision set. Only bones within the skeleton's joint_count
 * are affected. The skeleton must be a per-instance copy, NOT the
 * template from the skeleton registry.
 *
 * @param skel_copy  Skeleton instance copy to modify (non-NULL, owned by caller).
 * @param set        Per-bone collision data (non-NULL).
 * @return true on success, false on invalid args.
 *
 * Ownership: allocates new colliders and hull_vertices arrays; frees
 *            any existing colliders/hull_vertices on the skeleton.
 */
bool mesh_bone_collision_override_skeleton(
    struct skeleton_def *skel_copy,
    const mesh_bone_collision_set_t *set);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_BONE_COLLISION_H */
