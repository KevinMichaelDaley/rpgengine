/**
 * @file mesh_merge.h
 * @brief Vertex merge (weld) and edge collapse operations.
 *
 * Types: mesh_merge_target_t (enum).
 * Functions: mesh_merge_vertices, mesh_merge_by_distance, mesh_collapse_edge.
 */
#ifndef MESH_MERGE_H
#define MESH_MERGE_H

#include "ferrum/editor/mesh/mesh_slot.h"
#include "ferrum/editor/mesh/mesh_edit.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Target position for vertex merge.
 */
typedef enum mesh_merge_target {
    MESH_MERGE_CENTER = 0, /**< Centroid of selected vertices. */
    MESH_MERGE_CURSOR = 1, /**< Specified cursor position. */
    MESH_MERGE_FIRST  = 2, /**< First selected vertex (lowest index). */
    MESH_MERGE_LAST   = 3  /**< Last selected vertex (highest index). */
} mesh_merge_target_t;

/**
 * @brief Merge selected vertices to a target position.
 *
 * All selected vertices are moved to the target, index references
 * updated, and degenerate triangles removed.
 *
 * @param slot    Mesh to modify. Not NULL.
 * @param sel     Vertex selection bitset. Not NULL.
 * @param target  Where to merge (center, cursor, first, last).
 * @param cursor  Cursor position (3 floats), required if target==CURSOR.
 * @return true on success, false on empty selection or error.
 *
 * Ownership: caller owns all parameters. Slot modified in-place.
 */
bool mesh_merge_vertices(mesh_slot_t *slot, const mesh_sel_bitset_t *sel,
                         mesh_merge_target_t target, const float *cursor);

/**
 * @brief Auto-weld vertices within threshold distance.
 *
 * O(V²) brute-force — suitable for editor meshes (< 65K verts).
 *
 * @param slot      Mesh to modify. Not NULL.
 * @param threshold Maximum distance for welding.
 * @return true on success.
 */
bool mesh_merge_by_distance(mesh_slot_t *slot, float threshold);

/**
 * @brief Collapse a single edge to its midpoint.
 *
 * Replaces both vertices with their midpoint. Removes degenerate
 * triangles created by the collapse.
 *
 * @param slot Mesh to modify. Not NULL.
 * @param v0   First vertex of edge.
 * @param v1   Second vertex of edge.
 * @return true on success.
 */
bool mesh_collapse_edge(mesh_slot_t *slot, uint32_t v0, uint32_t v1);

#endif /* MESH_MERGE_H */
