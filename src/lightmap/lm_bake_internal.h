/**
 * @file lm_bake_internal.h
 * @brief Private phase declarations shared between the bake orchestrator files.
 *        Not a public API; exposes no types.
 */
#ifndef FERRUM_LIGHTMAP_LM_BAKE_INTERNAL_H
#define FERRUM_LIGHTMAP_LM_BAKE_INTERNAL_H

#include "ferrum/lightmap/lm_bake.h"
#include "ferrum/math/vec3.h"
#include "ferrum/npc/npc_svo.h"

/**
 * @brief Geometry phase: allocate + fill the combined luxel array (positions,
 *        normals, albedo, emissive, per-luxel areas, surface offsets), pack the
 *        atlas, and stamp every surface into @p svo with its material id. Writes
 *        the luxel-centre array to @p *out_positions. Returns false on failure.
 */
bool lm_bake_build_geometry(const lm_scene_t *scene,
                            const lm_bake_config_t *cfg, lm_bake_result_t *res,
                            npc_svo_grid_t *svo, vec3_t **out_positions,
                            arena_t *arena);

/**
 * @brief Lighting phase: direct (emissive/area) + analytic-light indirect seed
 *        + SVO far-field, then progressive radiosity over all luxels using a
 *        kd-tree built on @p positions. Returns false on failure.
 */
bool lm_bake_run_lighting(const lm_scene_t *scene, const lm_bake_config_t *cfg,
                          lm_bake_result_t *res, const npc_svo_grid_t *svo,
                          const vec3_t *positions, arena_t *arena);

#endif /* FERRUM_LIGHTMAP_LM_BAKE_INTERNAL_H */
