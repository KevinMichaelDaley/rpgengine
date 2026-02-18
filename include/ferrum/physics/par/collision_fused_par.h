#ifndef FERRUM_PHYSICS_PAR_COLLISION_FUSED_PAR_H
#define FERRUM_PHYSICS_PAR_COLLISION_FUSED_PAR_H

/**
 * @file collision_fused_par.h
 * @brief Fused collision pipeline: narrow→manifold→stab→constraint in one dispatch.
 *
 * Instead of 4 separate dispatch+barrier cycles, processes each batch of
 * broadphase pairs through all 4 stages within a single fiber.  Saves 3
 * barrier round-trips per substep (~3ms at 2 substeps with 6 workers).
 *
 * Output: populates shared manifold and constraint arrays via atomics.
 * Manifolds needed by island build; constraints by TGS solver.
 */

#include <stdint.h>

#include "ferrum/physics/phys_jobs.h"
#include "ferrum/physics/phys_pool.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/broadphase.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/manifold.h"
#include "ferrum/physics/manifold_cache.h"
#include "ferrum/physics/narrowphase.h"
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/stabilization.h"
#include "ferrum/physics/constraint.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Batch size: broadphase pairs per fused job. */
#define PHYS_COLLISION_FUSED_BATCH_SIZE 128

/**
 * @brief Arguments for the fused collision pipeline.
 *
 * Combines inputs from narrowphase, manifold build, stabilization,
 * and constraint build into a single struct.
 */
typedef struct phys_collision_fused_args {
    /* ── Narrowphase inputs ─────────────────────────────────────── */
    const phys_body_t          *bodies;       /**< Body array (read-only). */
    const phys_collider_t      *colliders;    /**< Per-body collider descriptors. */
    const phys_sphere_t        *spheres;      /**< Sphere shape array. */
    const phys_box_t           *boxes;        /**< Box shape array. */
    const phys_capsule_t       *capsules;     /**< Capsule shape array. */
    const phys_mesh_shape_t    *meshes;       /**< Mesh shape array. */
    const phys_convex_hull_t   *convex_hulls; /**< Convex hull shape array. */
    const phys_halfspace_t     *halfspaces;   /**< Halfspace shape array. */
    const phys_collision_pair_t *pairs;       /**< Broadphase pair array. */
    uint32_t                    pair_count;   /**< Number of broadphase pairs. */
    float                       speculative_margin; /**< Speculative contact margin. */

    /* ── Manifold cache ─────────────────────────────────────────── */
    struct phys_manifold_cache *cache;        /**< Shared manifold cache. */
    uint64_t                    tick;         /**< Current simulation tick. */

    /* ── Stabilization parameters ───────────────────────────────── */
    float resting_velocity_threshold;         /**< Threshold for resting classification. */

    /* ── Constraint build parameters ────────────────────────────── */
    float dt;                                 /**< Substep dt. */
    float baumgarte;                          /**< Baumgarte stabilization coefficient. */
    float slop;                               /**< Penetration slop. */

    /* ── Outputs ────────────────────────────────────────────────── */
    phys_manifold_t    *manifolds_out;        /**< Output manifold buffer. */
    uint32_t           *manifold_count_out;   /**< Written manifold count. */
    uint32_t            max_manifolds;        /**< Capacity of manifolds_out. */

    phys_constraint_t  *constraints_out;      /**< Output constraint buffer. */
    uint32_t           *constraint_count_out; /**< Written constraint count. */
    uint32_t            max_constraints;      /**< Capacity of constraints_out. */
} phys_collision_fused_args_t;

/**
 * @brief Run the fused collision pipeline in parallel.
 *
 * Dispatches batches of broadphase pairs, each running narrow→manifold→
 * stab→constraint sequentially within a single fiber.  Uses atomic
 * counters for manifold and constraint output slot allocation.
 *
 * @param args   Fused collision arguments (must not be NULL).
 * @param ctx    Physics job context for dispatch (must not be NULL).
 * @param arena  Frame arena for batch descriptor allocation.
 *
 * Nullability: all pointer args must be non-NULL.
 * Side effects: writes to manifolds_out, constraints_out, cache.
 */
void phys_stage_collision_fused_par(const phys_collision_fused_args_t *args,
                                     phys_job_context_t *ctx,
                                     phys_frame_arena_t *arena);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_PHYSICS_PAR_COLLISION_FUSED_PAR_H */
