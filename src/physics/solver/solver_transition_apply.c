/**
 * @file solver_transition_apply.c
 * @brief Batch solver mode transition for constraints (phys-403).
 *
 * Iterates a constraint array and converts lambda values between
 * TGS impulse-domain and XPBD position-domain when the solver mode
 * has changed since the previous tick.
 */

#include "ferrum/physics/solver_transition.h"

#include <stdint.h>

#include "ferrum/physics/constraint.h"
#include "ferrum/physics/step_plan.h"

void phys_solver_transition_apply(phys_constraint_t *constraints,
                                  uint32_t count,
                                  const uint8_t *prev_modes,
                                  float dt)
{
    if (!constraints || !prev_modes || dt <= 0.0f) { return; }

    for (uint32_t i = 0; i < count; i++) {
        uint8_t prev    = prev_modes[i];
        uint8_t current = constraints[i].solver_mode;

        if (prev == current) { continue; }

        if (prev == PHYS_SOLVER_TGS && current == PHYS_SOLVER_XPBD) {
            phys_solver_convert_tgs_to_xpbd(&constraints[i], dt);
        } else if (prev == PHYS_SOLVER_XPBD && current == PHYS_SOLVER_TGS) {
            phys_solver_convert_xpbd_to_tgs(&constraints[i], dt);
        }
    }
}
