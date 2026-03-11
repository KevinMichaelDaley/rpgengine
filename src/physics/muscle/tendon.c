/**
 * @file tendon.c
 * @brief Tendon series elastic element — equilibrium solver.
 *
 * 2 non-static functions:
 *   1. phys_tendon_params_init
 *   2. phys_tendon_equilibrium
 */

#include "ferrum/physics/muscle/tendon.h"
#include "ferrum/physics/muscle/force_curve.h"

#include <math.h>
#include <stddef.h>

/** Maximum Newton iterations for equilibrium solve. */
#define MAX_NEWTON_ITERS 10

/** Convergence tolerance for force balance (Newtons). */
#define FORCE_TOL 0.01f

/* ── Static helpers ───────────────────────────────────────────────── */

/**
 * Tendon force-length curve: stiff nonlinear spring.
 * Force = 0 when length <= slack_length.
 * Force = stiffness * max_force * ((length - slack) / (slack * ref_strain))^2
 * for length > slack_length.
 */
static float tendon_force_(const phys_tendon_params_t *t,
                            float max_force,
                            float tendon_length)
{
    if (tendon_length <= t->slack_length) { return 0.0f; }
    float strain = (tendon_length - t->slack_length) /
                   (t->slack_length * t->reference_strain);
    /* Quadratic force-strain curve. */
    return t->stiffness * max_force * strain * strain;
}

/**
 * Derivative of tendon force w.r.t. tendon length.
 */
static float tendon_force_deriv_(const phys_tendon_params_t *t,
                                  float max_force,
                                  float tendon_length)
{
    if (tendon_length <= t->slack_length) { return 0.0f; }
    float denom = t->slack_length * t->reference_strain;
    if (denom <= 0.0f) { return 0.0f; }
    float strain = (tendon_length - t->slack_length) / denom;
    return 2.0f * t->stiffness * max_force * strain / denom;
}

/* ── Public API ───────────────────────────────────────────────────── */

void phys_tendon_params_init(phys_tendon_params_t *p)
{
    if (!p) { return; }
    p->slack_length     = 0.20f;
    p->stiffness        = 35.0f;
    p->reference_strain = 0.033f;
}

void phys_tendon_equilibrium(const phys_tendon_params_t *tendon,
                              const phys_muscle_params_t *muscle,
                              float activation,
                              float total_length,
                              float fiber_hint,
                              phys_tendon_state_t *out)
{
    if (!tendon || !muscle || !out) { return; }

    float cos_penn = cosf(muscle->pennation_angle);
    if (cos_penn < 0.01f) { cos_penn = 0.01f; } /* Prevent division by zero. */

    /* Initial guess for fiber length. */
    float Lf = fiber_hint;
    if (Lf <= 0.0f) { Lf = muscle->optimal_length; }

    /* Clamp fiber length to valid range. */
    float min_Lf = muscle->optimal_length * 0.3f;
    float max_Lf = total_length / cos_penn;
    if (max_Lf < min_Lf) { max_Lf = min_Lf; }
    if (Lf < min_Lf) { Lf = min_Lf; }
    if (Lf > max_Lf) { Lf = max_Lf; }

    /* Newton iteration: solve F_muscle(Lf) - F_tendon(Lt) = 0
     * where Lt = total_length - Lf * cos(pennation). */
    for (int iter = 0; iter < MAX_NEWTON_ITERS; iter++) {
        float Lt = total_length - Lf * cos_penn;
        if (Lt < 0.0f) { Lt = 0.0f; }

        /* Muscle force at current fiber length (zero velocity for equilibrium). */
        float norm_len = Lf / muscle->optimal_length;
        phys_muscle_force_t mf;
        phys_muscle_force_compute(muscle, activation, norm_len, 0.0f, &mf);
        float F_muscle = mf.f_total;

        /* Tendon force at current tendon length. */
        float F_tendon = tendon_force_(tendon, muscle->max_force, Lt);

        float residual = F_muscle - F_tendon;
        if (fabsf(residual) < FORCE_TOL) { break; }

        /* Jacobian: d(residual)/d(Lf).
         * dF_muscle/dLf ≈ finite difference approximation.
         * dF_tendon/dLf = -dF_tendon/dLt * cos_penn. */
        float dLf = muscle->optimal_length * 0.001f;
        if (dLf < 1e-6f) { dLf = 1e-6f; }
        float norm_len2 = (Lf + dLf) / muscle->optimal_length;
        phys_muscle_force_t mf2;
        phys_muscle_force_compute(muscle, activation, norm_len2, 0.0f, &mf2);
        float dFm_dLf = (mf2.f_total - F_muscle) / dLf;

        float dFt_dLf = -tendon_force_deriv_(tendon, muscle->max_force, Lt)
                         * cos_penn;

        float jacobian = dFm_dLf - dFt_dLf;
        if (fabsf(jacobian) < 1e-10f) { break; }

        float step = -residual / jacobian;

        /* Damped Newton step to prevent overshooting. */
        float new_Lf = Lf + step;
        if (new_Lf < min_Lf) { new_Lf = min_Lf; }
        if (new_Lf > max_Lf) { new_Lf = max_Lf; }
        Lf = new_Lf;
    }

    /* Write output. */
    float Lt = total_length - Lf * cos_penn;
    if (Lt < 0.0f) { Lt = 0.0f; }

    out->fiber_length  = Lf;
    out->tendon_length = Lt;
    out->tendon_force  = tendon_force_(tendon, muscle->max_force, Lt);
}
