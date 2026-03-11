/**
 * @file force_curve.c
 * @brief Hill-type muscle force model — curve evaluations.
 *
 * 2 non-static functions:
 *   1. phys_muscle_params_init
 *   2. phys_muscle_force_compute
 */

#include "ferrum/physics/muscle/force_curve.h"

#include <math.h>
#include <stddef.h>

/* ── Static curve helpers ─────────────────────────────────────────── */

/**
 * Active force-length curve: Gaussian-like bell centered at L=1.
 * f_active(L) = exp(-((L - 1) / width)^2)
 * where L is normalized fiber length and width controls breadth.
 */
static float active_force_length_(float norm_length, float width)
{
    if (width <= 0.0f) { return 0.0f; }
    float x = (norm_length - 1.0f) / width;
    return expf(-(x * x));
}

/**
 * Passive force-length curve: exponential rise at long lengths.
 * f_passive(L) = max(0, (exp(k * (L - 1)) - 1) / (exp(k) - 1))
 * where k controls the steepness.  Zero force below optimal length.
 */
static float passive_force_length_(float norm_length)
{
    if (norm_length <= 1.0f) { return 0.0f; }
    /* k = 5.0 gives reasonable passive curve shape. */
    float k = 5.0f;
    float denom = expf(k) - 1.0f;
    if (denom <= 0.0f) { return 0.0f; }
    float num = expf(k * (norm_length - 1.0f)) - 1.0f;
    return num / denom;
}

/**
 * Force-velocity curve (Hill equation).
 * Concentric (shortening, V > 0): f_v = (1 - V) / (1 + V / a_hill)
 *   where V is normalized velocity and a_hill is the shape parameter.
 * Eccentric (lengthening, V < 0): linear ramp up to 1.4x isometric.
 */
static float force_velocity_(float norm_velocity)
{
    /* a_hill shape parameter (typical: 0.25). */
    float a_hill = 0.25f;

    if (norm_velocity >= 1.0f) {
        /* At or beyond max shortening velocity: zero force. */
        return 0.0f;
    }

    if (norm_velocity >= 0.0f) {
        /* Concentric (shortening): Hill hyperbola. */
        float denom = 1.0f + norm_velocity / a_hill;
        if (denom <= 0.0f) { return 0.0f; }
        return (1.0f - norm_velocity) / denom;
    }

    /* Eccentric (lengthening): linear ramp to 1.4x at V = -1. */
    float ecc = 1.0f + 0.4f * (-norm_velocity);
    if (ecc > 1.8f) { ecc = 1.8f; } /* Cap at 1.8x isometric. */
    return ecc;
}

/* ── Public API ───────────────────────────────────────────────────── */

void phys_muscle_params_init(phys_muscle_params_t *p)
{
    if (!p) { return; }
    p->optimal_length  = 0.1f;
    p->max_force       = 100.0f;
    p->max_velocity    = 10.0f;
    p->pennation_angle = 0.0f;
    p->width           = 0.56f;
}

void phys_muscle_force_compute(const phys_muscle_params_t *params,
                                float activation,
                                float norm_length,
                                float norm_velocity,
                                phys_muscle_force_t *out)
{
    if (!params || !out) { return; }

    /* Clamp activation. */
    if (activation < 0.0f) { activation = 0.0f; }
    if (activation > 1.0f) { activation = 1.0f; }

    /* Evaluate individual curves. */
    float fa = active_force_length_(norm_length, params->width);
    float fp = passive_force_length_(norm_length);
    float fv = force_velocity_(norm_velocity);

    out->f_active   = fa;
    out->f_passive  = fp;
    out->f_velocity = fv;

    /* Account for pennation angle: force along tendon = F * cos(alpha). */
    float cos_penn = cosf(params->pennation_angle);

    out->f_total = (activation * fa * fv + fp) * params->max_force * cos_penn;
}
