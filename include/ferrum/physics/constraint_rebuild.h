#ifndef FERRUM_PHYSICS_CONSTRAINT_REBUILD_H
#define FERRUM_PHYSICS_CONSTRAINT_REBUILD_H

/** @file
 * @brief Constraint rebuild functions for inter-iteration Jacobian updates.
 *
 * During nonlinear implicit Gauss-Seidel solving, body positions change
 * between iterations.  This module provides functions to rebuild both
 * joint and contact constraint Jacobians, biases, and effective masses
 * from updated body state.
 *
 * Used by the coupled TGS solver (TIER_ANIM) and sub-substep paths.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declarations. */
struct phys_island;
struct phys_constraint;
struct phys_body;
struct phys_joint;
struct phys_manifold;
struct phys_mat3;

/**
 * @brief Arguments for rebuilding all constraints in an island.
 *
 * Ownership: caller owns all pointers.  No allocations performed.
 */
typedef struct phys_constraint_rebuild_args {
    struct phys_constraint *constraints;    /**< Global constraint array. */
    uint32_t constraint_count;              /**< Total number of constraints. */
    const uint32_t *constraint_joint_indices; /**< Maps constraint index -> joint index. */
    struct phys_joint *joints;              /**< Joint array (rebuilt in-place). */
    uint32_t joint_count;                   /**< Number of joints. */
    const struct phys_body *bodies;         /**< Body array (updated positions). */
    uint32_t body_count;                    /**< Number of bodies. */
    const struct phys_manifold *manifolds;  /**< Manifold array for contact rebuild. */
    uint32_t manifold_count;                /**< Number of manifolds. */
    const struct phys_mat3 *inv_inertia_world; /**< World-space inverse inertia. */
    float dt;                               /**< Substep timestep. */
    float baumgarte;                        /**< Baumgarte stabilization factor. */
    float slop;                             /**< Penetration slop threshold. */
} phys_constraint_rebuild_args_t;

/**
 * @brief Rebuild joint constraint rows for an island from updated body state.
 *
 * For each joint constraint in the island, calls the appropriate joint
 * build function with current body positions, then packs the rebuilt
 * rows back into the constraint.  Accumulated impulses (lambdas) are
 * preserved for warm-starting across iterations.
 *
 * Non-joint constraints are skipped.
 *
 * @param island  The island whose joint constraints to rebuild.
 * @param args    Rebuild arguments (all pointers must be valid).
 *
 * Side effects: modifies constraints[].rows and joints[].rows.
 */
void phys_rebuild_island_joint_constraints(
    const struct phys_island *island,
    const phys_constraint_rebuild_args_t *args);

/**
 * @brief Rebuild contact constraint rows for an island from updated body state.
 *
 * For each contact constraint in the island, recomputes the contact
 * geometry (world-space contact point, penetration depth) from the
 * manifold's local-space contact coordinates and the updated body
 * transforms.  Rebuilds Jacobians, bias, and effective mass.
 * Accumulated impulses (lambdas) are preserved.
 *
 * Joint constraints are skipped.
 *
 * @param island  The island whose contact constraints to rebuild.
 * @param args    Rebuild arguments (manifolds must be non-NULL).
 *
 * Side effects: modifies constraints[].rows.
 */
void phys_rebuild_island_contact_constraints(
    const struct phys_island *island,
    const phys_constraint_rebuild_args_t *args);

/**
 * @brief Rebuild ALL constraint rows (joints + contacts) for an island.
 *
 * Calls phys_rebuild_island_joint_constraints() followed by
 * phys_rebuild_island_contact_constraints().
 *
 * @param island  The island whose constraints to rebuild.
 * @param args    Rebuild arguments.
 *
 * Side effects: modifies constraints[].rows and joints[].rows.
 */
void phys_rebuild_island_all_constraints(
    const struct phys_island *island,
    const phys_constraint_rebuild_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONSTRAINT_REBUILD_H */
