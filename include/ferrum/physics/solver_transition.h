#ifndef FERRUM_PHYSICS_SOLVER_TRANSITION_H
#define FERRUM_PHYSICS_SOLVER_TRANSITION_H

/** @file
 * @brief Warm-start conversion for bodies crossing the TGS/XPBD boundary.
 *
 * When a body is promoted (T2→T1) or demoted (T1→T2), its accumulated
 * constraint lambdas must be converted between impulse-domain (TGS)
 * and position-domain (XPBD) to preserve warm-start continuity.
 */

struct phys_constraint;

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
