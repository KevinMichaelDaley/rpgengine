/**
 * @file lm_bake.c
 * @brief Lightmap bake orchestrator + atlas readback (see lm_bake.h).
 */
#include "ferrum/lightmap/lm_bake.h"

#include "ferrum/lightmap/lm_sh.h"
#include "lm_bake_internal.h"

bool lm_bake(const lm_scene_t *scene, const lm_bake_config_t *config,
             lm_bake_result_t *result, arena_t *arena)
{
    npc_svo_grid_t svo;
    if (!npc_svo_grid_init(&svo, config->svo_bounds, config->svo_depth))
        return false;

    vec3_t *positions = NULL;
    bool ok = lm_bake_build_geometry(scene, config, result, &svo, &positions,
                                     arena);
    if (ok && result->n_luxels > 0)
        ok = lm_bake_run_lighting(scene, config, result, &svo, positions, arena);

    npc_svo_grid_destroy(&svo);
    return ok;
}

void lm_bake_readback(const lm_bake_result_t *result, float *out_rgb)
{
    uint32_t w = result->atlas.width;
    uint32_t h = result->atlas.height;
    for (uint32_t i = 0; i < w * h * 3; ++i)
        out_rgb[i] = 0.0f;

    for (uint32_t s = 0; s < result->n_surfaces; ++s) {
        const lm_atlas_rect_t *rect = &result->rects[s];
        uint32_t base = result->surface_offsets[s];
        for (uint32_t iv = 0; iv < rect->h; ++iv) {
            for (uint32_t iu = 0; iu < rect->w; ++iu) {
                const lm_luxel_t *lx =
                    &result->combined.luxels[base + iv * rect->w + iu];
                uint32_t px = rect->x + iu;
                uint32_t py = rect->y + iv;
                float *dst = &out_rgb[(py * w + px) * 3];
                for (int c = 0; c < 3; ++c) {
                    float e = lm_sh9_irradiance(&lx->sh[c], lx->normal);
                    dst[c] = e > 0.0f ? e : 0.0f;
                }
            }
        }
    }
}
