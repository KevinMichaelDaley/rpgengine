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
#include "ferrum/lightmap/lm_sky.h"
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
    uint32_t    svo_depth;       /**< SVO subdivision depth (used if voxel_size<=0). */
    float       voxel_size;      /**< Target voxel edge (m) for this region; if
                                      >0 the baker derives svo_depth from it.
                                      Default 0 -> use svo_depth. ~0.01 typical. */
    uint32_t    atlas_width;     /**< Output atlas width (luxels). */
    uint32_t    atlas_padding;   /**< Gutter between packed surfaces. */
    uint32_t    direct_samples;  /**< Area-light samples per luxel. */
    uint32_t    farfield_samples;/**< SVO hemisphere rays per luxel. */
    float       farfield_near;   /**< Near cutoff for the far-field gather. */
    float       farfield_maxdist;/**< Max far-field ray length. */
    lm_sky_t    sky;             /**< Environment sky for escaping far-field rays. */
    uint32_t    gi_bounces;      /**< Path-traced GI bounce depth (0 = direct-lit
                                      near surfaces only; 2-3 typical). */
    uint32_t    gi_threads;      /**< Gather thread count (0 = all online CPUs). */
    uint32_t    gi_batch;        /**< Gather samples per progressive batch (0 ->
                                      64). farfield_samples is reached by summing
                                      ceil(farfield_samples/gi_batch) batches and
                                      averaging, so no single huge per-luxel
                                      gather is ever done. */
    /** Optional progress hook, invoked after each batch is folded into the
     *  luxel SH (so @p result is renderable for a progressive preview). @p done
     *  / @p total are batch counts. NULL to disable. */
    void      (*on_batch)(void *ud, uint32_t done, uint32_t total);
    void       *on_batch_ud;     /**< User data passed to @ref on_batch. */
    lm_solve_params_t solve;     /**< Radiosity solve parameters + region gate. */
    uint32_t    seed;            /**< Base RNG seed. */
    int         gpu_gather;      /**< Run the GI gather on the GPU (rpg-k4lk); the
                                      caller must lm_gpu_gather_init() a compute
                                      context first. 0 = CPU path. */
    float       chunk_size;      /**< GPU chunked bake (rpg-fzht): if >0, partition
                                      the scene into cubic chunks of this edge and
                                      build a per-chunk SDF instead of one field
                                      over the whole scene. 0 = single region. */
    float       chunk_margin;    /**< Overlap added to each chunk's SDF box so rays
                                      resolve across chunk boundaries (m). */
    /** Per-chunk atlas bake (rpg-yfa4): geometry used for the GI gather (SVO +
     *  SDF fields) when it must differ from the luxelized @ref lm_mesh_scene
     *  meshes -- i.e. bake ONE chunk's meshes into their own atlas while still
     *  occluding/bouncing against the WHOLE scene. NULL -> use the scene's own
     *  meshes (the normal single-atlas bake). Borrowed. */
    const struct lm_mesh_scene *geo_scene;
    /** GPU bake (rpg-iudw): if non-NULL, persist each near chunk's baked SDF to
     *  "<sdf_out_prefix>_cNNN.sdf" (chunked path) for runtime reuse. Borrowed;
     *  NULL disables SDF export. */
    const char *sdf_out_prefix;
} lm_bake_config_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SCENE_H */
