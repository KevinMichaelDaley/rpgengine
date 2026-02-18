/**
 * @file convex_decompose.h
 * @brief Approximate convex decomposition of triangle meshes.
 *
 * Decomposes a triangle mesh into a set of approximate convex hulls
 * using volumetric approximate convex decomposition (V-ACD):
 *   1. Voxelize the mesh surface + flood-fill interior
 *   2. Iteratively split concave regions along max-concavity plane
 *   3. Build convex hull of each piece's voxel centers
 *
 * Public types (2):
 *   1. phys_decompose_params_t  — decomposition parameters
 *   2. phys_decompose_result_t  — output hull array
 */

#ifndef FERRUM_PHYSICS_CONVEX_DECOMPOSE_H
#define FERRUM_PHYSICS_CONVEX_DECOMPOSE_H

#include <stdint.h>

#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/mesh_collider.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Maximum convex pieces produced by decomposition. */
#define PHYS_DECOMPOSE_MAX_HULLS 64u

/** Maximum voxel grid resolution per axis. */
#define PHYS_DECOMPOSE_MAX_RESOLUTION 64u

/**
 * @brief Parameters controlling convex decomposition.
 *
 * Ownership: value type, no pointers.
 */
typedef struct phys_decompose_params {
    uint32_t resolution;        /**< Voxel grid resolution per axis (8–64). */
    float concavity_threshold;  /**< Max concavity before splitting (0.01–1.0). */
    uint32_t max_hulls;         /**< Max number of output hulls (1–64). */
    uint32_t min_voxels;        /**< Min voxels per cluster to keep (default: 4). */
} phys_decompose_params_t;

/**
 * @brief Result of convex decomposition.
 *
 * Contains up to PHYS_DECOMPOSE_MAX_HULLS convex hulls.
 * Ownership: self-contained, copy-safe.
 */
typedef struct phys_decompose_result {
    phys_convex_hull_t hulls[PHYS_DECOMPOSE_MAX_HULLS];
    uint32_t hull_count;
} phys_decompose_result_t;

/* ── Public API ────────────────────────────────────────────────── */

/**
 * @brief Return default decomposition parameters.
 *
 * @return Default params: resolution=32, concavity=0.05, max_hulls=32,
 *         min_voxels=4.
 *
 * Side effects: none.
 */
phys_decompose_params_t phys_decompose_params_default(void);

/**
 * @brief Decompose a triangle mesh into convex hulls.
 *
 * @param triangles  Input triangle array (non-NULL if tri_count > 0).
 * @param tri_count  Number of input triangles.
 * @param params     Decomposition parameters.
 * @param result     Output result (non-NULL, zeroed by caller).
 * @return 0 on success, -1 on error (NULL args, invalid params).
 *
 * Ownership: result is fully self-contained after call.
 * Side effects: allocates temporary memory via malloc (freed before return).
 */
int phys_decompose_mesh(const phys_triangle_t *triangles,
                        uint32_t tri_count,
                        const phys_decompose_params_t *params,
                        phys_decompose_result_t *result);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONVEX_DECOMPOSE_H */
