/**
 * @file lm_indirect.c
 * @brief Analytic-light indirect seeding (see lm_indirect.h).
 */
#include "ferrum/lightmap/lm_indirect.h"

#include "ferrum/lightmap/lm_visibility.h"
#include "ferrum/math/vec3.h"

/* Direct irradiance a single light deposits on a single luxel, cosine-weighted
 * and SVO-shadowed. Adds into @p accum (RGB); no-op if unreachable/shadowed. */
static void lm_indirect_add_light(const lm_luxel_t *luxel,
                                  const lm_light_t *light,
                                  const npc_svo_grid_t *svo, vec3_t *accum)
{
    vec3_t dir, irradiance;
    float dist;
    if (!lm_light_incident(light, luxel->pos, &dir, &dist, &irradiance))
        return;
    float cos_recv = vec3_dot(dir, luxel->normal);
    if (cos_recv <= 0.0f)
        return; /* light is behind the surface */
    if (svo && lm_visibility_occluded(svo, luxel->pos, dir, dist))
        return; /* in shadow */
    *accum = vec3_add(*accum, vec3_scale(irradiance, cos_recv));
}

void lm_indirect_direct_irradiance(const lm_lightmap_t *lm,
                                   const lm_light_t *lights, uint32_t n_lights,
                                   const npc_svo_grid_t *svo,
                                   float *out_irradiance)
{
    uint32_t n_luxels = lm->res_u * lm->res_v;
    for (uint32_t i = 0; i < n_luxels; ++i) {
        const lm_luxel_t *luxel = &lm->luxels[i];
        vec3_t accum = { 0.0f, 0.0f, 0.0f };
        for (uint32_t j = 0; j < n_lights; ++j)
            lm_indirect_add_light(luxel, &lights[j], svo, &accum);
        out_irradiance[i * 3 + 0] = accum.x;
        out_irradiance[i * 3 + 1] = accum.y;
        out_irradiance[i * 3 + 2] = accum.z;
    }
}
