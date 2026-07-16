/**
 * @file gi_probe_kernel.c
 * @brief Dynamic-light probe update kernel (see gi_probe_kernel.h).
 */
#include "ferrum/renderer/gi/gi_probe_kernel.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/lightmap/lm_sh.h"

/* Sphere-march the combined SDF from @p o along unit @p d for a SOFT visibility
 * in [0,1] (Quilez-style: penumbra from the closest cone approach). Stops at
 * @p maxdist (the light distance). */
static float soft_vis(const float *dist, const int32_t dims[3],
                      const float origin[3], float voxel,
                      const gi_collider_t *col, uint32_t ncol,
                      const float o[3], const float d[3], float maxdist,
                      uint32_t steps, float k)
{
    float res = 1.0f;
    float t = 0.05f;                     /* start bias off the probe. */
    for (uint32_t i = 0; i < steps && t < maxdist; ++i) {
        float p[3] = { o[0]+d[0]*t, o[1]+d[1]*t, o[2]+d[2]*t };
        float h = gi_sdf_combined(dist, dims, origin, voxel, col, ncol, p);
        if (h < 0.001f)
            return 0.0f;                 /* hit an occluder. */
        float cone = k * h / t;
        if (cone < res) res = cone;
        float step = h;                  /* advance by the safe distance, clamped. */
        if (step < 0.02f) step = 0.02f;
        if (step > 0.5f) step = 0.5f;
        t += step;
    }
    return res < 0.0f ? 0.0f : (res > 1.0f ? 1.0f : res);
}

/* Smooth range attenuation for point/spot lights (0 at/after range). */
static float range_atten(float dist, float range)
{
    if (range <= 0.0f) return 1.0f;
    float x = dist / range;
    if (x >= 1.0f) return 0.0f;
    float f = 1.0f - x * x;
    return f * f;                        /* smooth windowed inverse-ish falloff. */
}

/* Accumulate one light's contribution into probe SH blocks (r,g,b). */
static void add_light(const gi_light_t *L, const float ppos[3],
                      const float *dist, const int32_t dims[3],
                      const float origin[3], float voxel,
                      const gi_collider_t *col, uint32_t ncol,
                      uint32_t steps, float k,
                      lm_sh9_t *shr, lm_sh9_t *shg, lm_sh9_t *shb)
{
    float dir[3], maxdist, atten = 1.0f;
    if (L->kind == GI_LIGHT_DIRECTIONAL) {
        float len = sqrtf(L->dir[0]*L->dir[0]+L->dir[1]*L->dir[1]+L->dir[2]*L->dir[2]);
        if (len < 1e-6f) return;
        for (int a = 0; a < 3; ++a) dir[a] = -L->dir[a] / len; /* toward the sun. */
        maxdist = 1e4f;                  /* march far for a directional shadow. */
    } else {
        float to[3] = { L->pos[0]-ppos[0], L->pos[1]-ppos[1], L->pos[2]-ppos[2] };
        float d = sqrtf(to[0]*to[0]+to[1]*to[1]+to[2]*to[2]);
        if (d < 1e-5f) return;
        for (int a = 0; a < 3; ++a) dir[a] = to[a] / d;
        maxdist = d;
        atten = range_atten(d, L->range);
        if (atten <= 0.0f) return;
        if (L->kind == GI_LIGHT_SPOT) {
            /* Cone: -dir is toward the light; compare with the light travel dir. */
            float ll = sqrtf(L->dir[0]*L->dir[0]+L->dir[1]*L->dir[1]+L->dir[2]*L->dir[2]);
            if (ll < 1e-6f) return;
            float cd = (-dir[0]*L->dir[0] - dir[1]*L->dir[1] - dir[2]*L->dir[2]) / ll;
            float t = (cd - L->cos_outer) / (L->cos_inner - L->cos_outer + 1e-6f);
            if (t <= 0.0f) return;
            if (t > 1.0f) t = 1.0f;
            atten *= t * t * (3.0f - 2.0f * t); /* smoothstep. */
        }
    }

    float vis = soft_vis(dist, dims, origin, voxel, col, ncol, ppos, dir,
                         maxdist, steps, k);
    if (vis <= 0.0f) return;
    float s = vis * atten;
    vec3_t L3 = { dir[0], dir[1], dir[2] };
    lm_sh9_add_sample(shr, L3, L->color[0] * s, 1.0f);
    lm_sh9_add_sample(shg, L3, L->color[1] * s, 1.0f);
    lm_sh9_add_sample(shb, L3, L->color[2] * s, 1.0f);
}

void gi_probe_kernel_update(gi_probe_set_t *set, uint32_t from, uint32_t to,
                            const float *dist, const int32_t dims[3],
                            const float origin[3], float voxel,
                            const gi_collider_t *colliders, uint32_t n_col,
                            const gi_light_t *lights, uint32_t n_lights,
                            uint32_t march_steps, float soft_k)
{
    if (set == NULL || set->pos == NULL || set->sh == NULL)
        return;
    if (to > set->count) to = set->count;

    for (uint32_t i = from; i < to; ++i) {
        const float *ppos = &set->pos[i * 3];
        lm_sh9_t shr, shg, shb;
        lm_sh9_zero(&shr); lm_sh9_zero(&shg); lm_sh9_zero(&shb);
        for (uint32_t l = 0; l < n_lights; ++l)
            add_light(&lights[l], ppos, dist, dims, origin, voxel,
                      colliders, n_col, march_steps, soft_k, &shr, &shg, &shb);
        for (int b = 0; b < 9; ++b) {
            set->sh[i*27 + 0*9 + b] = shr.c[b];
            set->sh[i*27 + 1*9 + b] = shg.c[b];
            set->sh[i*27 + 2*9 + b] = shb.c[b];
        }
    }
}
