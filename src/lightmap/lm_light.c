/**
 * @file lm_light.c
 * @brief Analytic light incident illumination (see lm_light.h).
 */
#include "ferrum/lightmap/lm_light.h"

#include <math.h>

/* Smooth window that fades inverse-square falloff to exactly 0 at `range`
 * (UE-style (1 - (d/range)^4)^2), so a bounded light has no hard edge. */
static float lm_range_window(float dist, float range)
{
    if (range <= 0.0f)
        return 1.0f;
    float r = dist / range;
    if (r >= 1.0f)
        return 0.0f;
    float r4 = r * r * r * r;
    float w = 1.0f - r4;
    return w * w;
}

bool lm_light_incident(const lm_light_t *light, vec3_t point, vec3_t *out_dir,
                       float *out_dist, vec3_t *out_irradiance)
{
    if (light->kind == LM_LIGHT_DIRECTIONAL) {
        /* Rays travel along `direction`; the light lies the opposite way. */
        vec3_t to_light = vec3_scale(vec3_normalize_safe(light->direction, 1e-8f),
                                     -1.0f);
        *out_dir = to_light;
        *out_dist = 1.0e6f;                 /* effectively infinite shadow ray */
        *out_irradiance = light->color;     /* no distance falloff */
        return true;
    }

    /* Point / spot: vector from the shading point to the light. */
    vec3_t delta = vec3_sub(light->position, point);
    float dist = vec3_magnitude(delta);
    if (dist < 1e-6f)
        return false;
    vec3_t to_light = vec3_scale(delta, 1.0f / dist);

    float window = lm_range_window(dist, light->range);
    if (window <= 0.0f)
        return false;

    /* Inverse-square, softened near the origin to avoid a singularity. */
    float atten = window / (dist * dist);

    if (light->kind == LM_LIGHT_SPOT) {
        /* Cone factor: cosine between the emission axis and the ray FROM the
         * light to the point, smoothstepped across the penumbra. */
        vec3_t axis = vec3_normalize_safe(light->direction, 1e-8f);
        float cosang = vec3_dot(axis, vec3_scale(to_light, -1.0f));
        if (cosang <= light->cos_outer)
            return false;
        float denom = light->cos_inner - light->cos_outer;
        float t = denom > 1e-6f ? (cosang - light->cos_outer) / denom : 1.0f;
        if (t > 1.0f) t = 1.0f;
        if (t < 0.0f) t = 0.0f;
        atten *= t * t * (3.0f - 2.0f * t);   /* smoothstep */
    }

    *out_dir = to_light;
    *out_dist = dist;
    *out_irradiance = vec3_scale(light->color, atten);
    return true;
}
