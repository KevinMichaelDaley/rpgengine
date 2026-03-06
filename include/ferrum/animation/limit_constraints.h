/**
 * @file limit_constraints.h
 * @brief Limit constraint evaluators (Rotation, Location, Scale).
 *
 * Public types: 0
 * Public functions: 1 (limit_constraints_register)
 */

#ifndef FERRUM_ANIMATION_LIMIT_CONSTRAINTS_H
#define FERRUM_ANIMATION_LIMIT_CONSTRAINTS_H

#include "ferrum/animation/constraint_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register Limit Rotation, Limit Location, and Limit Scale evaluators.
 * @param solver Solver to register with (non-NULL).
 */
void limit_constraints_register(constraint_solver_t *solver);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_LIMIT_CONSTRAINTS_H */
