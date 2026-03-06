/**
 * @file surface_vol.h
 * @brief Surface and volume constraint evaluators.
 *
 * Public types: 0
 * Public functions: 1 (surface_vol_register)
 */

#ifndef FERRUM_ANIMATION_SURFACE_VOL_H
#define FERRUM_ANIMATION_SURFACE_VOL_H

#include "ferrum/animation/constraint_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register Floor, Clamp To, Shrinkwrap, and Maintain Volume evaluators.
 *
 * Clamp To and Shrinkwrap are stubbed (require curve/mesh query infrastructure).
 *
 * @param solver Solver to register with (non-NULL).
 */
void surface_vol_register(constraint_solver_t *solver);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_SURFACE_VOL_H */
