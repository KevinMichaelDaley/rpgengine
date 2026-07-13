/**
 * @file lm_scene.h
 * @brief Scene description and bake configuration for the lightmap baker.
 *
 * A scene is a set of diffuse surfaces (each a lm_surface quad with a lightmap
 * resolution, albedo, emissive, and a material id), a set of analytic lights,
 * and a material table used to shade SVO far-field reflectors. The bake config
 * carries the SVO bounds/depth, atlas layout, sample counts, and the radiosity
 * solve parameters (including the partial-bake gate).
 *
 * Ownership: all arrays are borrowed by the baker for the duration of the bake.
 * Nullability: array pointers non-NULL when their count > 0. Offline.
 */
#ifndef FERRUM_LIGHTMAP_LM_SCENE_H
#define FERRUM_LIGHTMAP_LM_SCENE_H

#include <stdint.h>

#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_material.h"
#include "ferrum/lightmap/lm_solve.h"
#include "ferrum/lightmap/lm_types.h"
#include "ferrum/physics/aabb.h"

#ifdef __cplusplus
extern "C" {
#endif

/** A bakeable scene: surfaces (+ their material ids), lights, material table. */
typedef struct lm_scene {
    const lm_surface_t  *surfaces;          /**< Diffuse quads to bake. */
    const uint16_t      *surface_materials; /**< Material id per surface (SVO). */
    uint32_t             n_surfaces;
    const lm_light_t    *lights;            /**< Analytic (indirect) lights. */
    uint32_t             n_lights;
    lm_material_table_t  materials;         /**< id -> albedo/emissive. */
} lm_scene_t;

/** Tunables for one bake: acceleration structures, sampling, and the solve. */
typedef struct lm_bake_config {
    phys_aabb_t svo_bounds;      /**< World bounds of the SVO. */
    uint32_t    svo_depth;       /**< SVO subdivision depth. */
    uint32_t    atlas_width;     /**< Output atlas width (luxels). */
    uint32_t    atlas_padding;   /**< Gutter between packed surfaces. */
    uint32_t    direct_samples;  /**< Area-light samples per luxel. */
    uint32_t    farfield_samples;/**< SVO hemisphere rays per luxel. */
    float       farfield_near;   /**< Near cutoff for the far-field gather. */
    float       farfield_maxdist;/**< Max far-field ray length. */
    lm_solve_params_t solve;     /**< Radiosity solve parameters + region gate. */
    uint32_t    seed;            /**< Base RNG seed. */
} lm_bake_config_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SCENE_H */
