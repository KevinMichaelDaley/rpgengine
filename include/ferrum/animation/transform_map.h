/**
 * @file transform_map.h
 * @brief Transform mapping, Child Of, and Pivot constraint evaluators.
 *
 * Public types: 0
 * Public functions: 1 (transform_map_register)
 */

#ifndef FERRUM_ANIMATION_TRANSFORM_MAP_H
#define FERRUM_ANIMATION_TRANSFORM_MAP_H

#include "ferrum/animation/constraint_solver.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Register Transformation, Child Of, and Pivot evaluators.
 *
 * Action constraint is left as no-op (requires animation clip system).
 *
 * @param solver Solver to register with (non-NULL).
 */
void transform_map_register(constraint_solver_t *solver);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_TRANSFORM_MAP_H */
