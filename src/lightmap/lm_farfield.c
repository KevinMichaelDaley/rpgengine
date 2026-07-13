/**
 * @file lm_farfield.c
 * @brief SVO far-field distant-reflector gather (see lm_farfield.h).
 */
#include "ferrum/lightmap/lm_farfield.h"

#include <math.h>

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/math/vec3.h"

#define LM_FF_TWO_PI 6.28318530717958648f

/* Deterministic LCG float draw in [0,1). */
static float lm_ff_rngf(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) * (1.0f / 16777216.0f);
}

/* Build an orthonormal basis (t, b) spanning the plane perpendicular to n. */
static void lm_ff_basis(vec3_t n, vec3_t *t, vec3_t *b)
{
    /* Pick the least-aligned axis to avoid a degenerate cross product. */
    vec3_t up = fabsf(n.z) < 0.9f ? (vec3_t){ 0, 0, 1 } : (vec3_t){ 1, 0, 0 };
    *t = vec3_normalize_safe(vec3_cross(up, n), 1e-6f);
    *b = vec3_cross(n, *t);
}

/* Gather far-field radiance into one luxel by tracing a uniform hemisphere. */
static void lm_ff_gather_luxel(lm_luxel_t *luxel, const npc_svo_grid_t *svo,
                               const lm_material_table_t *table, uint32_t samples,
                               float near_radius, float max_dist, uint32_t *rng)
{
    vec3_t t, b;
    lm_ff_basis(luxel->normal, &t, &b);
    /* Small bias off the surface so the origin voxel is not re-hit. */
    vec3_t origin = vec3_add(luxel->pos, vec3_scale(luxel->normal, 1e-3f));
    float weight = LM_FF_TWO_PI / (float)samples; /* solid angle per sample */

    for (uint32_t s = 0; s < samples; ++s) {
        /* Uniform hemisphere direction about the normal. */
        float u1 = lm_ff_rngf(rng);
        float u2 = lm_ff_rngf(rng);
        float z = u1;                       /* cos of polar angle, uniform */
        float r = sqrtf(1.0f - z * z);
        float phi = LM_FF_TWO_PI * u2;
        vec3_t dir = vec3_add(vec3_add(vec3_scale(t, r * cosf(phi)),
                                       vec3_scale(b, r * sinf(phi))),
                              vec3_scale(luxel->normal, z));

        lm_ray_hit_t hit;
        if (!lm_visibility_trace(svo, origin, dir, max_dist, &hit))
            continue;
        if (hit.t <= near_radius)
            continue; /* near field -- handled by the shooting solver */

        lm_material_t mat = lm_material_lookup(table, hit.material);
        const float *em = &mat.emissive.x;
        for (int c = 0; c < 3; ++c)
            lm_sh9_add_sample(&luxel->sh[c], dir, em[c], weight);
    }
}

void lm_farfield_gather(lm_lightmap_t *lm, const npc_svo_grid_t *svo,
                        const lm_material_table_t *table, uint32_t samples,
                        float near_radius, float max_dist, uint32_t seed)
{
    if (samples == 0)
        return;
    uint32_t n_luxels = lm->res_u * lm->res_v;
    for (uint32_t i = 0; i < n_luxels; ++i) {
        uint32_t rng = seed ^ (i * 2654435761u);
        lm_ff_gather_luxel(&lm->luxels[i], svo, table, samples, near_radius,
                           max_dist, &rng);
    }
}
