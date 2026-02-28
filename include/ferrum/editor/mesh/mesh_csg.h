/**
 * @file mesh_csg.h
 * @brief Constructive solid geometry operations on meshes.
 *
 * Types: none (uses mesh_slot.h).
 * Functions: mesh_csg_hollow, mesh_csg_merge, mesh_csg_subtract, mesh_csg_intersect.
 *
 * CSG operations work on triangle-soup meshes. The boolean operations
 * (merge/subtract/intersect) use a simplified approach: combine geometry
 * from both meshes, classifying faces as inside/outside via raycasting.
 *
 * Ownership: result meshes are written to caller-owned slots.
 * Nullability: NULL returns false.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_CSG_H
#define FERRUM_EDITOR_MESH_CSG_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdbool.h>

/**
 * @brief Hollow out a solid mesh by creating an inner shell.
 *
 * Duplicates all geometry, offsets inner vertices inward along normals
 * by @p thickness, and flips inner face winding. The result is a
 * thin-walled hollow version of the original.
 *
 * @param slot       Mesh to hollow. Modified in place. Not NULL.
 * @param thickness  Wall thickness (positive). Must be > 0.
 * @return true on success.
 */
bool mesh_csg_hollow(mesh_slot_t *slot, float thickness);

/**
 * @brief Boolean union of two meshes.
 *
 * Combines both meshes, keeping faces that are on the outside of
 * the combined volume.
 *
 * @param a       First mesh. Not NULL.
 * @param b       Second mesh. Not NULL.
 * @param result  Output mesh (cleared first). Not NULL.
 * @return true on success.
 */
bool mesh_csg_merge(const mesh_slot_t *a, const mesh_slot_t *b,
                    mesh_slot_t *result);

/**
 * @brief Boolean subtraction (a minus b).
 *
 * Removes the volume of @p cutter from @p target.
 *
 * @param target  Base mesh. Not NULL.
 * @param cutter  Mesh to subtract. Not NULL.
 * @param result  Output mesh (cleared first). Not NULL.
 * @return true on success.
 */
bool mesh_csg_subtract(const mesh_slot_t *target, const mesh_slot_t *cutter,
                       mesh_slot_t *result);

/**
 * @brief Boolean intersection of two meshes.
 *
 * Keeps only the volume where both meshes overlap.
 *
 * @param a       First mesh. Not NULL.
 * @param b       Second mesh. Not NULL.
 * @param result  Output mesh (cleared first). Not NULL.
 * @return true on success.
 */
bool mesh_csg_intersect(const mesh_slot_t *a, const mesh_slot_t *b,
                        mesh_slot_t *result);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_CSG_H */
