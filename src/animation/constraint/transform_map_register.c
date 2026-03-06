/**
 * @file transform_map_register.c
 * @brief Register Transformation, Child Of, and Pivot evaluators.
 *
 * Non-static functions: 1 (transform_map_register)
 */

#include "ferrum/animation/transform_map.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/* External evaluators. */
extern void eval_transformation(const constraint_def_t *def,
                                const constraint_eval_ctx_t *ctx,
                                mat4_t *inout);
extern void eval_child_of(const constraint_def_t *def,
                          const constraint_eval_ctx_t *ctx,
                          mat4_t *inout);
extern void eval_pivot(const constraint_def_t *def,
                       const constraint_eval_ctx_t *ctx,
                       mat4_t *inout);

void transform_map_register(constraint_solver_t *solver) {
    if (!solver) return;
    constraint_solver_register_eval(solver, CONSTRAINT_TRANSFORMATION, eval_transformation);
    constraint_solver_register_eval(solver, CONSTRAINT_CHILD_OF,       eval_child_of);
    constraint_solver_register_eval(solver, CONSTRAINT_PIVOT,          eval_pivot);
    /* CONSTRAINT_ACTION left as no-op stub — requires animation clip system. */
}
