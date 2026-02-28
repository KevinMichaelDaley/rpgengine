/**
 * @file mesh_brush.h
 * @brief Create convex mesh from intersection of half-planes.
 *
 * Types: mesh_brush_plane_t (plane definition).
 * Functions: mesh_create_from_brush.
 *
 * Each plane clips the initial bounding volume, producing the convex
 * hull of the half-space intersection (TrenchBroom/Quake brush style).
 *
 * Ownership: caller owns the mesh_slot_t.
 * Nullability: NULL returns false.
 * Thread safety: not thread-safe.
 */
#ifndef FERRUM_EDITOR_MESH_BRUSH_H
#define FERRUM_EDITOR_MESH_BRUSH_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief A plane defined by outward normal and distance from origin.
 *
 * The plane equation is: dot(normal, P) <= dist
 * i.e., the interior is on the negative side of each plane.
 */
typedef struct mesh_brush_plane {
    float normal[3]; /**< Outward-pointing normal (should be unit). */
    float dist;      /**< Distance from origin along normal. */
} mesh_brush_plane_t;

/**
 * @brief Create a convex mesh from intersection of half-planes.
 *
 * Starts with a large axis-aligned bounding box, then clips with each
 * plane in sequence. The result is the convex solid bounded by all
 * planes. Normals are recalculated to face outward.
 *
 * @param slot       Output mesh (cleared first). Not NULL.
 * @param planes     Array of clipping planes. Not NULL.
 * @param num_planes Number of planes.
 * @return true on success, false on NULL or degenerate result.
 *
 * Side effects: clears and rebuilds slot.
 */
bool mesh_create_from_brush(mesh_slot_t *slot,
                            const mesh_brush_plane_t *planes,
                            uint32_t num_planes);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_EDITOR_MESH_BRUSH_H */
