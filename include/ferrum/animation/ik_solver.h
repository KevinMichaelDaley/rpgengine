/**
 * @file ik_solver.h
 * @brief IK solver API: CCD and FABRIK algorithms.
 *
 * Both solvers operate on world-space pose matrices and modify
 * them in place. They are called by the constraint solver dispatch
 * or can be used directly.
 *
 * Public types: 0 (uses types from constraint_solver.h)
 * Public functions: 3 (ik_solve_ccd, ik_solve_fabrik, ik_solver_register)
 */

#ifndef FERRUM_ANIMATION_IK_SOLVER_H
#define FERRUM_ANIMATION_IK_SOLVER_H

#include <stdint.h>
#include "ferrum/animation/constraint_params.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Solve IK using Cyclic Coordinate Descent.
 *
 * Iterates from tip to root of the chain, rotating each bone
 * to minimize the distance from end-effector to target.
 *
 * @param skel          Skeleton definition (for parent indices).
 * @param pose          World-space pose matrices (in/out).
 * @param tip_bone_idx  Index of the tip (end-effector) bone in the IK chain.
 * @param chain_length  Number of bones in the IK chain (tip to root).
 *                      chain_length=0 is a no-op.
 * @param target        World-space target position.
 * @param max_iter      Maximum solver iterations.
 * @param tolerance     Convergence tolerance (distance).
 */
void ik_solve_ccd(const skeleton_def_t *skel, mat4_t *pose,
                  uint32_t tip_bone_idx, uint32_t chain_length,
                  vec3_t target, uint32_t max_iter, float tolerance);

/**
 * @brief Solve IK using Forward And Backward Reaching (FABRIK).
 *
 * Alternates forward (root→tip, enforce bone lengths) and backward
 * (tip→root, pull toward target) passes. Preserves bone segment
 * lengths exactly.
 *
 * @param skel          Skeleton definition.
 * @param pose          World-space pose matrices (in/out).
 * @param tip_bone_idx  Index of the tip (end-effector) bone.
 * @param chain_length  Number of bones in the IK chain.
 * @param target        World-space target position.
 * @param max_iter      Maximum solver iterations.
 * @param tolerance     Convergence tolerance (distance).
 */
void ik_solve_fabrik(const skeleton_def_t *skel, mat4_t *pose,
                     uint32_t tip_bone_idx, uint32_t chain_length,
                     vec3_t target, uint32_t max_iter, float tolerance);

/**
 * @brief Register IK constraint evaluators in the solver dispatch table.
 *
 * Registers CONSTRAINT_IK → CCD-based evaluator.
 * (CONSTRAINT_SPLINE_IK is left as no-op for now.)
 *
 * @param solver Solver to register with (non-NULL).
 */
void ik_solver_register(constraint_solver_t *solver);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_IK_SOLVER_H */
