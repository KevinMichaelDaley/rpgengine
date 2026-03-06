/**
 * @file limit_register.c
 * @brief Register Limit constraint evaluators in the solver dispatch table.
 *
 * Non-static functions: 1 (limit_constraints_register)
 */

#include "ferrum/animation/limit_constraints.h"
#include "ferrum/animation/constraint_solver.h"
#include "ferrum/animation/constraint_types.h"
#include "ferrum/animation/constraint_params.h"
#include "ferrum/math/mat4.h"

/* External evaluators. */
extern void eval_limit_rotation(const constraint_def_t *def,
                                const constraint_eval_ctx_t *ctx,
                                mat4_t *inout);
extern void eval_limit_location(const constraint_def_t *def,
                                const constraint_eval_ctx_t *ctx,
                                mat4_t *inout);
extern void eval_limit_scale(const constraint_def_t *def,
                             const constraint_eval_ctx_t *ctx,
                             mat4_t *inout);

void limit_constraints_register(constraint_solver_t *solver) {
    if (!solver) return;
    constraint_solver_register_eval(solver, CONSTRAINT_LIMIT_ROTATION, eval_limit_rotation);
    constraint_solver_register_eval(solver, CONSTRAINT_LIMIT_LOCATION, eval_limit_location);
    constraint_solver_register_eval(solver, CONSTRAINT_LIMIT_SCALE,    eval_limit_scale);
}
