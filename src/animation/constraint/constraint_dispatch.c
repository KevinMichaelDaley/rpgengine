/**
 * @file constraint_dispatch.c
 * @brief Dispatch table registration and lookup.
 *
 * Non-static functions: 2 (register_eval, get_eval_fn)
 */

#include "ferrum/animation/constraint_solver.h"
#include <stddef.h>

void constraint_solver_register_eval(constraint_solver_t *solver,
                                     constraint_type_t type,
                                     constraint_eval_fn fn) {
    if (!solver || !fn) return;
    if (!constraint_type_is_valid(type)) return;
    solver->dispatch[type] = fn;
}

constraint_eval_fn constraint_solver_get_eval_fn(const constraint_solver_t *solver,
                                                  constraint_type_t type) {
    if (!solver || !constraint_type_is_valid(type)) return NULL;
    return solver->dispatch[type];
}
