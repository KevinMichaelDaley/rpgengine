/**
 * @file lm_gi_gather.c
 * @brief Unified path-traced GI gather (see lm_gi_gather.h).
 */
#include "ferrum/lightmap/lm_gi_gather.h"

#include <math.h>

#include "ferrum/lightmap/lm_parallel.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_svo_mip.h"
#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/math/vec3.h"

#define LM_GI_TWO_PI 6.28318530717958648f
#define LM_GI_INV_PI 0.31830988618379067f
#define LM_GI_SHADOW_MAX 1.0e4f

/* Deterministic LCG float draw in [0,1). */
static float lm_gi_rngf(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return (float)(*state >> 8) * (1.0f / 16777216.0f);
}

/* Orthonormal basis (t, b) perpendicular to n. */
static void lm_gi_basis(vec3_t n, vec3_t *t, vec3_t *b)
{
    vec3_t up = fabsf(n.z) < 0.9f ? (vec3_t){ 0, 0, 1 } : (vec3_t){ 1, 0, 0 };
    *t = vec3_normalize_safe(vec3_cross(up, n), 1e-6f);
    *b = vec3_cross(n, *t);
}

/* Cosine-weighted hemisphere direction about n (Malley's method). */
static vec3_t lm_gi_cosine_dir(vec3_t n, uint32_t *rng)
{
    vec3_t t, b;
    lm_gi_basis(n, &t, &b);
    float u1 = lm_gi_rngf(rng), u2 = lm_gi_rngf(rng);
    float r = sqrtf(u1), z = sqrtf(1.0f - u1), phi = LM_GI_TWO_PI * u2;
    return vec3_add(vec3_add(vec3_scale(t, r * cosf(phi)),
                             vec3_scale(b, r * sinf(phi))),
                    vec3_scale(n, z));
}

/* Mip levels to climb for a far hit at distance @p t (cone footprint LOD). */
static uint32_t lm_gi_cone_levels(const npc_svo_grid_t *svo, float t,
                                  float solid_angle)
{
    float half_angle = sqrtf(solid_angle * LM_GI_INV_PI);
    float ratio = (2.0f * t * half_angle) / svo->voxel_size;
    uint32_t levels = 0;
    while (ratio > 1.0f && levels < svo->max_depth) {
        ratio *= 0.5f;
        ++levels;
    }
    return levels;
}

/* Direct incident irradiance at (@p p, @p n) from the analytic lights, shadow-
 * tested against the SVO. Returns sum of colour*cos over unoccluded lights. */
static vec3_t lm_gi_direct(const npc_svo_grid_t *svo, const lm_light_t *lights,
                           uint32_t nl, vec3_t p, vec3_t n)
{
    vec3_t e = { 0, 0, 0 };
    float bias = svo->voxel_size * 1.5f;
    vec3_t o = vec3_add(p, vec3_scale(n, bias));
    for (uint32_t i = 0; i < nl; ++i) {
        vec3_t to_light;
        float atten = 1.0f, maxdist = LM_GI_SHADOW_MAX;
        if (lights[i].kind == LM_LIGHT_DIRECTIONAL) {
            to_light = vec3_normalize_safe(vec3_scale(lights[i].direction, -1.0f), 1e-6f);
        } else {
            vec3_t d = vec3_sub(lights[i].position, p);
            float dist = vec3_magnitude(d);
            if (dist < 1e-4f) continue;
            to_light = vec3_scale(d, 1.0f / dist);
            atten = 1.0f / (dist * dist);
            maxdist = dist - bias;
        }
        float cosNL = vec3_dot(n, to_light);
        if (cosNL <= 0.0f)
            continue;
        if (lm_visibility_occluded(svo, o, to_light, maxdist))
            continue;
        float s = cosNL * atten;
        e.x += lights[i].color.x * s;
        e.y += lights[i].color.y * s;
        e.z += lights[i].color.z * s;
    }
    return e;
}

/* Path-trace one primary ray and return the incident radiance it carries. */
static vec3_t lm_gi_trace(const npc_svo_grid_t *svo, const lm_light_t *lights,
                          uint32_t nl, const lm_sky_t *sky, float transition,
                          float maxdist, float solid_angle, uint32_t bounces,
                          vec3_t origin, vec3_t dir, uint32_t *rng)
{
    vec3_t L = { 0, 0, 0 };
    vec3_t through = { 1, 1, 1 };
    float bias = svo->voxel_size * 1.5f;

    for (uint32_t bounce = 0; bounce <= bounces; ++bounce) {
        lm_ray_hit_t hit;
        if (!lm_visibility_trace(svo, origin, dir, maxdist, &hit)) {
            vec3_t s = sky ? lm_sky_sample(sky, dir) : (vec3_t){ 0, 0, 0 };
            L.x += through.x * s.x; L.y += through.y * s.y; L.z += through.z * s.z;
            break;
        }
        if (hit.t > transition) {
            /* Past the transition: cone the octree and approximate the distant
             * surface as sky-lit (+ its own emission). */
            uint32_t lv = lm_gi_cone_levels(svo, hit.t, solid_angle);
            lm_svo_shade_t sh = lm_svo_mip_sample(svo, hit.node, lv);
            vec3_t amb = sky ? lm_sky_sample(sky, dir) : (vec3_t){ 0, 0, 0 };
            L.x += through.x * (sh.emissive.x + sh.diffuse.x * amb.x);
            L.y += through.y * (sh.emissive.y + sh.diffuse.y * amb.y);
            L.z += through.z * (sh.emissive.z + sh.diffuse.z * amb.z);
            break;
        }
        /* Near hit: read the surface material and light it (emission + direct). */
        lm_svo_shade_t mat = lm_svo_mip_sample(svo, hit.node, 0u);
        L.x += through.x * mat.emissive.x;
        L.y += through.y * mat.emissive.y;
        L.z += through.z * mat.emissive.z;
        vec3_t E = lm_gi_direct(svo, lights, nl, hit.position, hit.normal);
        L.x += through.x * mat.diffuse.x * E.x * LM_GI_INV_PI;
        L.y += through.y * mat.diffuse.y * E.y * LM_GI_INV_PI;
        L.z += through.z * mat.diffuse.z * E.z * LM_GI_INV_PI;
        if (bounce == bounces)
            break;
        /* Scatter a cosine bounce; cosine-weighting cancels the cos/pi so the
         * throughput just picks up the albedo. */
        through.x *= mat.diffuse.x; through.y *= mat.diffuse.y; through.z *= mat.diffuse.z;
        dir = lm_gi_cosine_dir(hit.normal, rng);
        origin = vec3_add(hit.position, vec3_scale(hit.normal, bias));
    }
    return L;
}

/* Gather one luxel: a stratified hemisphere of path-traced primary rays. */
static void lm_gi_gather_luxel(lm_luxel_t *luxel, const npc_svo_grid_t *svo,
                               const lm_light_t *lights, uint32_t nl,
                               const lm_sky_t *sky, float transition,
                               float maxdist, uint32_t samples, uint32_t bounces,
                               uint32_t *rng)
{
    vec3_t t, b;
    lm_gi_basis(luxel->normal, &t, &b);
    float bias = svo->voxel_size * 1.5f;
    vec3_t origin = vec3_add(luxel->pos, vec3_scale(luxel->normal, bias));

    uint32_t n = (uint32_t)sqrtf((float)samples);
    if (n < 1u)
        n = 1u;
    float inv_n = 1.0f / (float)n;
    float weight = LM_GI_TWO_PI / (float)(n * n); /* solid angle per sample */

    for (uint32_t sy = 0; sy < n; ++sy) {
        for (uint32_t sx = 0; sx < n; ++sx) {
            float u1 = ((float)sx + lm_gi_rngf(rng)) * inv_n;
            float u2 = ((float)sy + lm_gi_rngf(rng)) * inv_n;
            float z = u1; /* uniform solid-angle primary (SH cosine lobe applies) */
            float r = sqrtf(1.0f - z * z);
            float phi = LM_GI_TWO_PI * u2;
            vec3_t dir = vec3_add(vec3_add(vec3_scale(t, r * cosf(phi)),
                                           vec3_scale(b, r * sinf(phi))),
                                  vec3_scale(luxel->normal, z));
            vec3_t Li = lm_gi_trace(svo, lights, nl, sky, transition, maxdist,
                                    weight, bounces, origin, dir, rng);
            const float *lc = &Li.x;
            for (int c = 0; c < 3; ++c)
                lm_sh9_add_sample(&luxel->sh[c], dir, lc[c], weight);
        }
    }
}

/* Shared read-only context for the parallel luxel gather. */
typedef struct lm_gi_ctx {
    lm_lightmap_t       *lm;
    const npc_svo_grid_t *svo;
    const lm_light_t    *lights;
    uint32_t             n_lights;
    const lm_sky_t      *sky;
    float                transition, maxdist;
    uint32_t             samples, bounces, seed;
} lm_gi_ctx_t;

/* Gather a contiguous chunk of luxels (one thread's share). */
static void lm_gi_chunk(uint32_t i0, uint32_t i1, void *vctx)
{
    lm_gi_ctx_t *c = (lm_gi_ctx_t *)vctx;
    for (uint32_t i = i0; i < i1; ++i) {
        uint32_t rng = c->seed ^ (i * 2654435761u);
        lm_gi_gather_luxel(&c->lm->luxels[i], c->svo, c->lights, c->n_lights,
                           c->sky, c->transition, c->maxdist, c->samples,
                           c->bounces, &rng);
    }
}

void lm_gi_gather(lm_lightmap_t *lm, const npc_svo_grid_t *svo,
                  const lm_light_t *lights, uint32_t n_lights,
                  const lm_sky_t *sky, float transition, float maxdist,
                  uint32_t samples, uint32_t bounces, uint32_t seed,
                  uint32_t n_threads)
{
    if (lm == NULL || svo == NULL || samples == 0)
        return;
    uint32_t n_luxels = lm->res_u * lm->res_v;
    /* Each luxel is independent (writes only its own SH; the SVO is read-only),
     * so split the luxels across the thread pool. */
    lm_gi_ctx_t ctx = { lm, svo, lights, n_lights, sky, transition, maxdist,
                        samples, bounces, seed };
    lm_parallel_for(n_luxels, lm_gi_chunk, &ctx, n_threads);
}
