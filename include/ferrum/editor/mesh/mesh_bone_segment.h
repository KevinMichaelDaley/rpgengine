/**
 * @file mesh_bone_segment.h
 * @brief Per-bone triangle segmentation for skeletal meshes.
 *
 * Segments a mesh's triangles by their dominant bone, producing
 * per-bone triangle lists suitable for convex decomposition.
 * Each triangle is assigned to whichever bone has the highest
 * total weight across the triangle's three vertices.
 *
 * Ownership: segments own their triangle arrays (heap-allocated).
 * Nullability: all pointer params must be non-NULL unless documented.
 * Error semantics: from_slot returns false on invalid args.
 * Side effects: allocates heap memory for per-bone triangle arrays.
 *
 * Public types: 2 (mesh_bone_segment_t, mesh_bone_segments_t).
 */
#ifndef FERRUM_EDITOR_MESH_BONE_SEGMENT_H
#define FERRUM_EDITOR_MESH_BONE_SEGMENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations. */
struct mesh_slot;
struct phys_triangle;

/**
 * @brief A segment of triangles belonging to a single bone.
 */
typedef struct mesh_bone_segment {
    struct phys_triangle *triangles; /**< Heap-allocated triangle array (owned). */
    uint32_t tri_count;              /**< Number of triangles in this segment. */
    uint32_t bone_index;             /**< Bone index this segment belongs to. */
} mesh_bone_segment_t;

/**
 * @brief Collection of per-bone triangle segments.
 */
typedef struct mesh_bone_segments {
    mesh_bone_segment_t *segments; /**< Heap-allocated segment array (owned). */
    uint32_t count;                /**< Number of active segments. */
    uint32_t capacity;             /**< Maximum segments (= max bone count). */
} mesh_bone_segments_t;

/**
 * @brief Initialize an empty segment set.
 *
 * @param segs      Output segment set (non-NULL).
 * @param capacity  Maximum number of bones/segments.
 */
void mesh_bone_segments_init(mesh_bone_segments_t *segs, uint32_t capacity);

/**
 * @brief Free all segment data.
 *
 * @param segs  Segment set to destroy (non-NULL, safe if already destroyed).
 */
void mesh_bone_segments_destroy(mesh_bone_segments_t *segs);

/**
 * @brief Segment mesh triangles by dominant bone.
 *
 * For each triangle, sums the bone weights across all three vertices
 * and assigns the triangle to the bone with the highest total weight.
 * Builds phys_triangle_t arrays from the mesh slot's positions and
 * indices.
 *
 * @param segs           Output segments (non-NULL, must be initialized).
 * @param slot           Mesh slot with positions and indices (non-NULL).
 * @param bone_indices   Bone index array (bones_per_vert uint32 per vertex).
 * @param bone_weights   Bone weight array (bones_per_vert float per vertex).
 * @param bones_per_vert Number of bone influences per vertex (typically 4).
 * @param bone_count     Total number of bones in the skeleton.
 * @return true on success, false on invalid args or empty mesh.
 *
 * Ownership: allocates triangle arrays within each segment.
 */
bool mesh_bone_segments_from_slot(mesh_bone_segments_t *segs,
                                   const struct mesh_slot *slot,
                                   const uint32_t *bone_indices,
                                   const float *bone_weights,
                                   uint32_t bones_per_vert,
                                   uint32_t bone_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_BONE_SEGMENT_H */
