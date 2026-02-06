#ifndef FERRUM_PHYSICS_STABILIZATION_H
#define FERRUM_PHYSICS_STABILIZATION_H

/** @file
 * @brief Stage 8: Stabilization Hints.
 *
 * Classifies contacts as resting or active and outputs per-manifold
 * friction/restitution scale factors to improve solver stability.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct phys_manifold;
struct phys_body;

/**
 * @brief Per-manifold stabilization hint.
 *
 * Multipliers applied to base friction and restitution coefficients.
 * Resting contacts get boosted friction (3.0) and suppressed bounce (0.0).
 * Active contacts pass through unmodified (1.0, 1.0).
 */
typedef struct phys_stab_hint {
    float friction_scale;      /**< Multiplier on base friction. */
    float restitution_scale;   /**< Multiplier on base restitution. */
} phys_stab_hint_t;

/**
 * @brief Arguments for the stabilization hints stage.
 *
 * @par Ownership
 * - manifolds, bodies: borrowed, read-only.
 * - hints_out: caller-owned output buffer, one entry per manifold.
 *
 * @par Nullability
 * - If args is NULL, the function is a no-op.
 * - If manifolds or hints_out is NULL, the function is a no-op.
 */
typedef struct phys_stabilization_args {
    const struct phys_manifold *manifolds;   /**< Array of manifolds. */
    uint32_t manifold_count;                 /**< Number of manifolds. */
    const struct phys_body *bodies;           /**< Body array (indexed by manifold body_a/body_b). */
    phys_stab_hint_t *hints_out;             /**< Output: one hint per manifold. */
    float resting_velocity_threshold;        /**< Speed below which contact is "resting". */
} phys_stabilization_args_t;

/**
 * @brief Classify each manifold's contact as resting or active.
 *
 * For each manifold, computes relative velocity at the first contact
 * point, decomposes it into normal and tangential components, and
 * assigns stabilization hints accordingly.
 *
 * @param args Stage arguments. If NULL, no-op.
 *
 * @par Side effects
 * Writes to args->hints_out[0..manifold_count-1].
 */
void phys_stage_stabilization(const phys_stabilization_args_t *args);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_STABILIZATION_H */
