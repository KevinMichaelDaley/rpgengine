/**
 * @file lm_farfield.c
 * @brief SVO far-field distant-reflector gather (see lm_farfield.h).
 */
#include "ferrum/lightmap/lm_farfield.h"

#include <math.h>

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_sky.h"
#include "ferrum/lightmap/lm_svo_mip.h"
#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/math/vec3.h"

#define LM_FF_TWO_PI 6.28318530717958648f
#define LM_FF_INV_PI 0.31830988618379067f

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

/* Mip levels to climb for a far hit at distance @p t: the ray's cone footprint
 * (2*t*half_angle) should roughly match the sampled voxel, so distant/wide rays
 * read a coarse, pre-filtered level (smooth, and cheap for very distant hits). */
static uint32_t lm_ff_cone_levels(const npc_svo_grid_t *svo, float t,
                                  float solid_angle)
{
    float half_angle = sqrtf(solid_angle * LM_FF_INV_PI);
    float ratio = (2.0f * t * half_angle) / svo->voxel_size;
    uint32_t levels = 0;
    while (ratio > 1.0f && levels < svo->max_depth) {
        ratio *= 0.5f;
        ++levels;
    }
    return levels;
}

/* Gather far-field radiance into one luxel with a STRATIFIED hemisphere of rays.
 * Escaping rays see the sky; far hits (t > near_radius) read the pre-filtered
 * SVO mip pyramid (self-emission + the surface's albedo reflecting the sky) at a
 * cone-selected level. Stratification + pre-filtering are what remove the
 * far-field speckle at a given ray budget. */
static void lm_ff_gather_luxel(lm_luxel_t *luxel, const npc_svo_grid_t *svo,
                               const lm_material_table_t *table,
                               const lm_sky_t *sky, uint32_t samples,
                               float near_radius, float max_dist, uint32_t *rng)
{
    (void)table; /* distant shading now comes from the pre-filtered SVO mip. */
    vec3_t t, b;
    lm_ff_basis(luxel->normal, &t, &b);
    /* Shadow bias: lift the origin a full 1.5 voxels off the surface, else the
     * ray starts inside the luxel's own solid voxel and self-occludes at t~=0,
     * silently killing every far-field / sky sample (matches lm_solve). */
    float eps = svo->voxel_size * 1.5f;
    vec3_t origin = vec3_add(luxel->pos, vec3_scale(luxel->normal, eps));

    /* Stratify the hemisphere into an NxN grid (N = floor(sqrt(samples))) with a
     * jitter per cell: far lower variance than pure random for the same count. */
    uint32_t n = (uint32_t)sqrtf((float)samples);
    if (n < 1u)
        n = 1u;
    float inv_n = 1.0f / (float)n;
    float weight = LM_FF_TWO_PI / (float)(n * n); /* solid angle per sample */

    for (uint32_t sy = 0; sy < n; ++sy) {
        for (uint32_t sx = 0; sx < n; ++sx) {
            float u1 = ((float)sx + lm_ff_rngf(rng)) * inv_n;
            float u2 = ((float)sy + lm_ff_rngf(rng)) * inv_n;
            float z = u1; /* cos polar, uniform in solid angle */
            float r = sqrtf(1.0f - z * z);
            float phi = LM_FF_TWO_PI * u2;
            vec3_t dir = vec3_add(vec3_add(vec3_scale(t, r * cosf(phi)),
                                           vec3_scale(b, r * sinf(phi))),
                                  vec3_scale(luxel->normal, z));

            lm_ray_hit_t hit;
            if (!lm_visibility_trace(svo, origin, dir, max_dist, &hit)) {
                /* Escaped the scene: this direction sees open sky. */
                if (sky != NULL) {
                    vec3_t s = lm_sky_sample(sky, dir);
                    const float *sc = &s.x;
                    for (int c = 0; c < 3; ++c)
                        lm_sh9_add_sample(&luxel->sh[c], dir, sc[c], weight);
                }
                continue;
            }
            if (hit.t <= near_radius)
                continue; /* near field -- handled by the shooting solver */

            /* Distant reflector: pre-filtered mip at the cone level, shaded as
             * self-emission plus its albedo reflecting the sky it sits under. */
            uint32_t levels = lm_ff_cone_levels(svo, hit.t, weight);
            lm_svo_shade_t shade = lm_svo_mip_sample(svo, hit.node, levels);
            vec3_t amb = sky ? lm_sky_sample(sky, dir) : (vec3_t){ 0, 0, 0 };
            float rad[3] = {
                shade.emissive.x + shade.diffuse.x * amb.x,
                shade.emissive.y + shade.diffuse.y * amb.y,
                shade.emissive.z + shade.diffuse.z * amb.z,
            };
            for (int c = 0; c < 3; ++c)
                lm_sh9_add_sample(&luxel->sh[c], dir, rad[c], weight);
        }
    }
}

void lm_farfield_gather(lm_lightmap_t *lm, const npc_svo_grid_t *svo,
                        const lm_material_table_t *table, const lm_sky_t *sky,
                        uint32_t samples, float near_radius, float max_dist,
                        uint32_t seed)
{
    if (samples == 0)
        return;
    uint32_t n_luxels = lm->res_u * lm->res_v;
    for (uint32_t i = 0; i < n_luxels; ++i) {
        uint32_t rng = seed ^ (i * 2654435761u);
        lm_ff_gather_luxel(&lm->luxels[i], svo, table, sky, samples, near_radius,
                           max_dist, &rng);
    }
}
