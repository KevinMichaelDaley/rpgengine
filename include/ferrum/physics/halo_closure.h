/**
 * @file halo_closure.h
 * @brief Stage 3 — Halo Closure for velocity-swept tier promotion.
 *
 * For each T0 body, expands its AABB by velocity * dt plus a margin,
 * queries the spatial grid for neighbors, and promotes eligible
 * neighbors to T1.  In Phase 1, all dynamic bodies start in T0
 * so this is largely a structural placeholder.
 */
#ifndef FERRUM_PHYSICS_HALO_CLOSURE_H
#define FERRUM_PHYSICS_HALO_CLOSURE_H

#include <stdint.h>

struct phys_body;
struct phys_aabb;
struct phys_spatial_grid;
struct phys_tier_lists;

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Arguments for the halo closure stage.
 *
 * Ownership: borrows all pointers; does not free anything.
 * Nullability: bodies, aabbs, grid, and tier_lists must be non-NULL
 *              for useful work.  NULL args pointer is a no-op.
 */
typedef struct phys_halo_closure_args {
    const struct phys_body *bodies;        /**< Body array (pool read buffer). */
    const struct phys_aabb *aabbs;         /**< Per-body AABBs. */
    const struct phys_spatial_grid *grid;  /**< Spatial hash grid. */
    struct phys_tier_lists *tier_lists;    /**< Modified in place — neighbors promoted. */
    float velocity_margin;                 /**< Extra padding around swept AABB. */
    float dt;                              /**< Timestep for sweep distance. */
    uint32_t body_count;                   /**< Number of bodies in the arrays. */
} phys_halo_closure_args_t;

/**
 * @brief Run the halo closure stage.
 *
 * For each T0 body:
 *   1. Copies its AABB.
 *   2. Extends by velocity * dt in the direction of motion.
 *   3. Expands by velocity_margin uniformly.
 *   4. Queries the grid for neighbors.
 *   5. Promotes dynamic neighbors not already in T0 to T1.
 *
 * @param args  Halo closure arguments (NULL-safe, no-op if NULL).
 *
 * Side effects: may add entries to args->tier_lists->tiers[PHYS_TIER_1_NEAR].
 */
void phys_stage_halo_closure(const phys_halo_closure_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_HALO_CLOSURE_H */
