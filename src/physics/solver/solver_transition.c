/**
 * @file solver_transition.c
 * @brief Warm-start conversion between TGS impulse-domain and XPBD
 *        position-domain lambda values.
 *
 * When bodies cross the T1↔T2 tier boundary, their accumulated
 * constraint impulses must be converted to preserve warm-start
 * continuity and avoid energy injection.
 */

#include "ferrum/physics/solver_transition.h"

#include <stdint.h>

#include "ferrum/physics/constraint.h"

void phys_solver_convert_tgs_to_xpbd(phys_constraint_t *c, float dt)
{
    if (!c || dt <= 0.0f) return;

    for (uint8_t r = 0; r < c->row_count; r++) {
        c->rows[r].lambda *= dt;
    }
}

void phys_solver_convert_xpbd_to_tgs(phys_constraint_t *c, float dt)
{
    if (!c || dt <= 0.0f) return;

    float inv_dt = 1.0f / dt;

    for (uint8_t r = 0; r < c->row_count; r++) {
        float lambda = c->rows[r].lambda * inv_dt;

        /* Clamp to prevent energy injection. */
        if (lambda < c->rows[r].lambda_min) lambda = c->rows[r].lambda_min;
        if (lambda > c->rows[r].lambda_max) lambda = c->rows[r].lambda_max;

        c->rows[r].lambda = lambda;
    }
}
