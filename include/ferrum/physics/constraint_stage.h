#ifndef FERRUM_PHYSICS_CONSTRAINT_STAGE_H
#define FERRUM_PHYSICS_CONSTRAINT_STAGE_H

/** @file
 * @brief Stage 9: Constraint Build.
 *
 * Generates Jacobian constraint rows from contact manifolds with
 * stabilization hints applied.  Each contact point produces one
 * constraint containing 3 rows (1 normal + 2 friction tangent).
 * Warmstart impulses are loaded from the manifold cache.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct phys_manifold;
struct phys_stab_hint;
struct phys_body;
struct phys_constraint;

/**
 * @brief Arguments for the constraint build stage.
 *
 * @par Ownership
 * - manifolds, hints, bodies: borrowed, read-only.
 * - constraints_out: caller-owned output buffer.
 * - constraint_count_out: caller-owned output scalar.
 *
 * @par Nullability
 * If args is NULL the function is a no-op.  If any required pointer
 * inside args is NULL the function is a no-op.
 */
typedef struct phys_constraint_build_args {
    const struct phys_manifold *manifolds;     /**< Array of manifolds.              */
    const struct phys_stab_hint *hints;        /**< Per-manifold stabilization hints. */
    uint32_t manifold_count;                   /**< Number of manifolds.             */
    const struct phys_body *bodies;            /**< Body array (indexed by manifold body_a/body_b). */
    struct phys_constraint *constraints_out;   /**< Output buffer for constraints.   */
    uint32_t *constraint_count_out;            /**< Output: number of constraints written. */
    uint32_t max_constraints;                  /**< Capacity of constraints_out.     */
    float dt;                                  /**< Timestep in seconds.             */
    float baumgarte;                           /**< Baumgarte stabilization factor.  */
    float slop;                                /**< Penetration slop threshold.      */
} phys_constraint_build_args_t;

/**
 * @brief Build constraints from contact manifolds (Stage 9).
 *
 * For each manifold and each contact point within it, constructs a
 * phys_constraint_t via phys_constraint_build_contact(), sets body
 * indices and manifold/point back-references, and loads warmstart
 * impulses from the manifold's cached impulse arrays.
 *
 * Stabilization hints scale friction and restitution before building.
 *
 * @param args  Stage arguments.  If NULL, no-op.
 *
 * @par Side effects
 * Writes to args->constraints_out and *args->constraint_count_out.
 */
void phys_stage_constraint_build(const phys_constraint_build_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_CONSTRAINT_STAGE_H */
