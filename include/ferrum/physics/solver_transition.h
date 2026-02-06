#ifndef FERRUM_PHYSICS_SOLVER_TRANSITION_H
#define FERRUM_PHYSICS_SOLVER_TRANSITION_H

/** @file
 * @brief Warm-start conversion for bodies crossing the TGS/XPBD boundary.
 *
 * When a body is promoted (T2→T1) or demoted (T1→T2), its accumulated
 * constraint lambdas must be converted between impulse-domain (TGS)
 * and position-domain (XPBD) to preserve warm-start continuity.
 */

#include <stdint.h>

struct phys_constraint;

/**
 * @brief Apply solver mode transitions to a batch of constraints.
 *
 * For each constraint whose current solver_mode differs from its
 * previous mode, applies the appropriate lambda conversion:
 *   - prev TGS, current XPBD → phys_solver_convert_tgs_to_xpbd
 *   - prev XPBD, current TGS → phys_solver_convert_xpbd_to_tgs
 *
 * @param constraints  Array of constraints. NULL-safe (no-op).
 * @param count        Number of constraints in the array.
 * @param prev_modes   Array of previous solver modes (one per constraint).
 *                     NULL-safe (no-op).
 * @param dt           Timestep (seconds). Must be > 0.
 *
 * @note Ownership: caller owns all pointers. No allocations performed.
 * @note Side effects: modifies lambda values in constraints that transition.
 */
void phys_solver_transition_apply(struct phys_constraint *constraints,
                                  uint32_t count,
                                  const uint8_t *prev_modes,
                                  float dt);

/**
 * @brief Convert TGS impulse-domain warmstart to XPBD position-domain.
 *
 * When a body is demoted from T1 → T2, its constraint lambdas
 * need conversion: λ_xpbd = λ_impulse * dt
 *
 * @param c   Constraint to convert. NULL-safe.
 * @param dt  Timestep (must be > 0).
 */
void phys_solver_convert_tgs_to_xpbd(struct phys_constraint *c, float dt);

/**
 * @brief Convert XPBD position-domain warmstart to TGS impulse-domain.
 *
 * When a body is promoted from T2 → T1, its constraint lambdas
 * need conversion: λ_impulse = clamp(λ_xpbd / dt, λ_min, λ_max)
 *
 * @param c   Constraint to convert. NULL-safe.
 * @param dt  Timestep (must be > 0).
 */
void phys_solver_convert_xpbd_to_tgs(struct phys_constraint *c, float dt);

#endif /* FERRUM_PHYSICS_SOLVER_TRANSITION_H */
