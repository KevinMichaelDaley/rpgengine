/**
 * @file lm_sky.c
 * @brief Environment sky sampler (see lm_sky.h).
 */
#include "ferrum/lightmap/lm_sky.h"

#include <math.h>
#include <stddef.h>

#define LM_SKY_PI     3.14159265358979324f
#define LM_SKY_INV_PI 0.31830988618379067f

vec3_t lm_sky_sample(const lm_sky_t *sky, vec3_t dir)
{
    if (sky == NULL)
        return (vec3_t){ 0.0f, 0.0f, 0.0f };

    if (sky->kind == LM_SKY_HDRI && sky->hdri != NULL &&
        sky->hdri->pixels != NULL) {
        /* Equirectangular lookup: longitude about +Y, latitude from y. */
        vec3_t d = vec3_normalize_safe(dir, 1e-6f);
        float u = 0.5f + (atan2f(d.x, d.z) + sky->yaw) * (0.5f * LM_SKY_INV_PI);
        float lat = d.y < -1.0f ? -1.0f : (d.y > 1.0f ? 1.0f : d.y);
        float v = 0.5f - asinf(lat) * LM_SKY_INV_PI; /* +Y -> v = 0 (top). */
        vec3_t c = lm_image_sample(sky->hdri, u, v);
        /* color is an exposure gain in the HDRI case. */
        return (vec3_t){ c.x * sky->color.x, c.y * sky->color.y,
                         c.z * sky->color.z };
    }

    /* Constant sky (also the fallback when an HDRI image is missing NULL). */
    return sky->color;
}
