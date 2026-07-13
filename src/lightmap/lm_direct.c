/**
 * @file lm_direct.c
 * @brief Direct lighting integrator (see lm_direct.h).
 */
#include "ferrum/lightmap/lm_direct.h"

#include <math.h>
#include <stddef.h>

#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/math/vec3.h"

/* Deterministic LCG + a [0,1) float draw, so a fixed seed gives a fixed bake. */
static uint32_t lm_direct_rng(uint32_t *state)
{
    *state = *state * 1664525u + 1013904223u;
    return *state;
}

static float lm_direct_rngf(uint32_t *state)
{
    return (float)(lm_direct_rng(state) >> 8) * (1.0f / 16777216.0f);
}

/* Accumulate one emitter's contribution into a single luxel's SH channels.
 * Stratifies @p total = ns*ns samples over the emitter quad; each carries the
 * emitter radiance weighted by emitter-cosine * area / (n * r^2), gated by an
 * SVO shadow ray. The receiver cosine is applied later at SH reconstruction. */
static void lm_direct_gather_emitter(lm_luxel_t *luxel, const lm_surface_t *e,
                                     const npc_svo_grid_t *svo, uint32_t ns,
                                     uint32_t *rng)
{
    float area = lm_surface_area(e);
    if (area <= 0.0f || ns == 0)
        return;
    float inv_n = 1.0f / (float)(ns * ns);
    float emissive[3] = { e->emissive.x, e->emissive.y, e->emissive.z };

    for (uint32_t su = 0; su < ns; ++su) {
        for (uint32_t sv = 0; sv < ns; ++sv) {
            /* Jittered stratified sample position on the emitter quad. */
            float fu = ((float)su + lm_direct_rngf(rng)) / (float)ns;
            float fv = ((float)sv + lm_direct_rngf(rng)) / (float)ns;
            vec3_t y = vec3_add(e->origin,
                                vec3_add(vec3_scale(e->edge_u, fu),
                                         vec3_scale(e->edge_v, fv)));

            vec3_t delta = vec3_sub(y, luxel->pos);
            float r2 = vec3_dot(delta, delta);
            if (r2 < 1e-8f)
                continue;
            float inv_r = 1.0f / sqrtf(r2);
            vec3_t dir = vec3_scale(delta, inv_r); /* luxel -> emitter */

            float cos_recv = vec3_dot(dir, luxel->normal);
            if (cos_recv <= 0.0f)
                continue; /* emitter is behind the luxel */
            float cos_emit = vec3_dot(vec3_scale(dir, -1.0f), e->normal);
            if (cos_emit <= 0.0f)
                continue; /* luxel is behind the emitter's front face */

            /* SVO shadow ray between the two surface points. Offset each end
             * off its surface along the normal by ~1.5 voxels so a point does
             * not self-occlude inside its own solid voxel (shadow bias). */
            if (svo) {
                float eps = svo->voxel_size * 1.5f;
                vec3_t o = vec3_add(luxel->pos, vec3_scale(luxel->normal, eps));
                vec3_t ye = vec3_add(y, vec3_scale(e->normal, eps));
                if (!lm_visibility_segment(svo, o, ye))
                    continue;
            }

            /* Solid-angle measure of this area sample as seen from the luxel. */
            float weight = cos_emit * area * inv_n / r2;
            for (int c = 0; c < 3; ++c)
                lm_sh9_add_sample(&luxel->sh[c], dir, emissive[c], weight);
        }
    }
}

void lm_direct_bake(lm_lightmap_t *lm, const lm_surface_t *emitters,
                    uint32_t n_emitters, const npc_svo_grid_t *svo,
                    uint32_t samples, uint32_t seed)
{
    /* Round the sample budget up to a square for stratification. */
    uint32_t ns = (uint32_t)ceilf(sqrtf((float)(samples ? samples : 1)));
    if (ns == 0)
        ns = 1;

    uint32_t n_luxels = lm->res_u * lm->res_v;
    for (uint32_t i = 0; i < n_luxels; ++i) {
        lm_luxel_t *luxel = &lm->luxels[i];
        /* Per-luxel stream keeps the bake deterministic and order-independent. */
        uint32_t rng = seed ^ (i * 2654435761u);
        for (uint32_t j = 0; j < n_emitters; ++j)
            lm_direct_gather_emitter(luxel, &emitters[j], svo, ns, &rng);
    }
}
