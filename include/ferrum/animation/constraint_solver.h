/**
 * @file constraint_solver.h
 * @brief Constraint solver API: evaluation loop and dispatch table.
 *
 * The solver evaluates all constraints on a skeleton in correct order
 * (parents before children, stack order within each bone). It outputs
 * target_pose[] which feeds into the physics motor system.
 *
 * Public types: 2 (constraint_solver_t, constraint_eval_ctx_t)
 * Public functions: constraint_eval_fn is a typedef, not a type.
 */

#ifndef FERRUM_ANIMATION_CONSTRAINT_SOLVER_H
#define FERRUM_ANIMATION_CONSTRAINT_SOLVER_H

#include <stdbool.h>
#include <stdint.h>
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Context passed to constraint evaluation functions.
 *
 * Provides read access to skeleton data and the current pose
 * so constraint evaluators can reference other bones.
 */
typedef struct constraint_eval_ctx {
    const skeleton_def_t *skel;       /**< Skeleton definition (read-only). */
    const mat4_t         *pose;       /**< Current pose matrices (read-only). */
    uint32_t              bone_count; /**< Number of bones in pose array. */
    uint32_t              bone_idx;   /**< Index of the bone being evaluated. */
} constraint_eval_ctx_t;

/**
 * @brief Constraint evaluation function signature.
 *
 * Called by the solver for each active constraint. The function
 * should read the constraint parameters and context, then modify
 * *inout_transform to apply the constraint effect.
 *
 * Influence blending is handled by the solver — the evaluator
 * should apply the constraint at full strength.
 *
 * @param def            Constraint definition (type + params).
 * @param ctx            Evaluation context (skeleton, pose, bone index).
 * @param inout_transform Current bone transform; modify in place.
 */
typedef void (*constraint_eval_fn)(const constraint_def_t *def,
                                   const constraint_eval_ctx_t *ctx,
                                   mat4_t *inout_transform);

/**
 * @brief Constraint solver with dispatch table.
 *
 * Manages per-type evaluation functions and workspace for solving
 * constraints on a skeleton. Allocated once, reused every tick.
 *
 * @note Owns workspace memory. Call constraint_solver_destroy() to free.
 */
typedef struct constraint_solver {
    uint32_t          max_bones;              /**< Maximum bone count. */
    uint32_t          max_constraints_per_bone; /**< Max constraint stack depth. */
    constraint_eval_fn dispatch[CONSTRAINT_TYPE_COUNT]; /**< Per-type evaluators. */
    mat4_t           *workspace;              /**< Temp matrix workspace. */
} constraint_solver_t;

/**
 * @brief Initialize a constraint solver.
 *
 * Allocates workspace and sets all dispatch entries to no-op stubs.
 *
 * @param solver              Output solver (non-NULL).
 * @param max_bones           Maximum number of bones to support.
 * @param max_constraints     Maximum constraints per bone.
 * @return true on success, false on invalid args or allocation failure.
 */
bool constraint_solver_init(constraint_solver_t *solver, uint32_t max_bones,
                            uint32_t max_constraints);

/**
 * @brief Free solver resources.
 * @param solver Solver to destroy (non-NULL).
 */
void constraint_solver_destroy(constraint_solver_t *solver);

/**
 * @brief Register an evaluation function for a constraint type.
 *
 * @param solver Solver instance (non-NULL).
 * @param type   Constraint type to register for.
 * @param fn     Evaluation function (non-NULL).
 */
void constraint_solver_register_eval(constraint_solver_t *solver,
                                     constraint_type_t type,
                                     constraint_eval_fn fn);

/**
 * @brief Get the registered evaluation function for a type.
 *
 * @param solver Solver instance (non-NULL).
 * @param type   Constraint type.
 * @return Registered function, or no-op stub if none registered.
 */
constraint_eval_fn constraint_solver_get_eval_fn(const constraint_solver_t *solver,
                                                  constraint_type_t type);

/**
 * @brief Evaluate all constraints on a skeleton.
 *
 * Iterates bones in skeleton order (parents before children).
 * For each bone, evaluates constraints in stack order (array index).
 * Applies influence blending after each constraint.
 *
 * The output is target_pose[] — the animation-solved bone transforms
 * that feed into the physics engine as motor targets.
 *
 * @param solver     Solver instance (non-NULL).
 * @param skel       Skeleton definition (non-NULL).
 * @param local_pose Local-space bone transforms used for FK propagation.
 *                   If non-NULL, each bone's world pose is computed as
 *                   parent_world × local before constraint evaluation.
 *                   If NULL, pose[] is used as-is (legacy world-space mode).
 * @param pose       Bone transform array (in/out, length >= bone_count).
 * @param bone_count Number of bones to evaluate.
 */
void constraint_solver_evaluate(constraint_solver_t *solver,
                                const skeleton_def_t *skel,
                                const mat4_t *local_pose,
                                mat4_t *pose, uint32_t bone_count);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_CONSTRAINT_SOLVER_H */
