/**
 * @file mesh_subdivide.h
 * @brief Mesh subdivision operations: linear and Loop.
 *
 * Public functions: mesh_subdivide_linear, mesh_subdivide_loop.
 */
#ifndef MESH_SUBDIVIDE_H
#define MESH_SUBDIVIDE_H

#include "ferrum/editor/mesh/mesh_slot.h"

#include <stdbool.h>
#include <stdint.h>

/**
 * @brief Linear (midpoint) subdivision.
 *
 * Each triangle is split into 4 by inserting midpoints on each edge.
 * No smoothing — vertices remain at exact midpoints.
 *
 * @param slot   Mesh to subdivide. Not NULL.
 * @param levels Number of subdivision levels (1+).
 * @return true on success, false on empty mesh or error.
 *
 * Ownership: caller owns slot. Modified in-place.
 */
bool mesh_subdivide_linear(mesh_slot_t *slot, uint32_t levels);

/**
 * @brief Loop subdivision.
 *
 * Triangle-specific smoothing subdivision using Loop's scheme.
 * Edge points use 3/8 + 3/8 + 1/8 + 1/8 weighting.
 * Original vertices repositioned based on valence.
 *
 * @param slot   Mesh to subdivide. Not NULL.
 * @param levels Number of subdivision levels (1+).
 * @return true on success, false on empty mesh or error.
 *
 * Ownership: caller owns slot. Modified in-place.
 */
bool mesh_subdivide_loop(mesh_slot_t *slot, uint32_t levels);

#endif /* MESH_SUBDIVIDE_H */
