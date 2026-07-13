/**
 * @file lm_bake_light.c
 * @brief Bake lighting phase: direct + indirect seed + far-field + solve.
 */
#include "lm_bake_internal.h"

#include <string.h>

#include "ferrum/lightmap/lm_direct.h"
#include "ferrum/lightmap/lm_farfield.h"
#include "ferrum/lightmap/lm_indirect.h"
#include "ferrum/lightmap/lm_kdtree.h"
#include "ferrum/lightmap/lm_solve.h"

bool lm_bake_run_lighting(const lm_scene_t *scene, const lm_bake_config_t *cfg,
                          lm_bake_result_t *res, const npc_svo_grid_t *svo,
                          const vec3_t *positions, arena_t *arena)
{
    lm_lightmap_t *lm = &res->combined;
    uint32_t total = res->n_luxels;

    /* 1. Full direct lighting from emissive/area surfaces. Non-emissive
     *    surfaces are passed too but contribute nothing (emissive == 0). */
    lm_direct_bake(lm, scene->surfaces, scene->n_surfaces, svo,
                   cfg->direct_samples, cfg->seed);

    /* 2. Analytic lights' first bounce -> indirect seed (not stored in SH). */
    float *seed = arena_alloc(arena, _Alignof(float), total * 3 * sizeof(float));
    if (!seed)
        return false;
    if (scene->n_lights > 0)
        lm_indirect_direct_irradiance(lm, scene->lights, scene->n_lights, svo,
                                      seed);
    else
        memset(seed, 0, total * 3 * sizeof(float));

    /* 3. Far-field distant reflectors/emitters via the SVO material table. */
    if (cfg->farfield_samples > 0)
        lm_farfield_gather(lm, svo, &scene->materials, &cfg->sky,
                           cfg->farfield_samples, cfg->farfield_near,
                           cfg->farfield_maxdist, cfg->seed ^ 0x9E3779B9u);

    /* 4. kd-tree over all luxel centres for near-field form factors. */
    lm_kdtree_t kd;
    if (!lm_kdtree_build(&kd, positions, total, arena))
        return false;

    /* 5. Progressive radiosity across every surface (cross-surface bleed). */
    lm_solver_t solver;
    if (!lm_solver_init(&solver, lm, &kd, svo, seed, res->luxel_areas, 0.0f,
                        arena))
        return false;
    lm_solver_run(&solver, &cfg->solve);
    return true;
}
