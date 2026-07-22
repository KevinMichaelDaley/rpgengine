/**
 * @file lm_gpu_gather.h
 * @brief GPU path of the GI gather (rpg-k4lk): a drop-in for @ref lm_gi_gather
 *        that runs the whole path-traced gather on the GPU.
 *
 * Ties the three validated compute stages together: rasterise the SVO occupancy
 * to a coarse dense grid, build a signed distance field by Jump Flood, pack the
 * SVO nodes + luxels + lights into SSBOs, and dispatch the gather kernel
 * (sphere-trace the SDF, refine into the SVO for material, multi-bounce, all
 * analytic light types + emissive voxels), reading the per-luxel SH9 back into
 * @p accum. Matches the CPU convention (uniform-hemisphere weight 2pi/n^2, Y
 * basis) so results are parity-comparable within Monte-Carlo tolerance.
 *
 * GL: needs a current GL 4.3+ (compute) context. The lightmap library is
 * headless, so GL is loaded via the caller's @ref gl_loader_t rather than glad.
 * State is file-static (offline, single-threaded bake use).
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_GATHER_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_GATHER_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_light.h"
#include "ferrum/lightmap/lm_lightmap.h"
#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/lightmap/lm_sh.h"
#include "ferrum/lightmap/lm_sky.h"
#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A GPU-resident coarse signed-distance field over an axis-aligned box.
 *
 * A dense JFA distance grid (@p dims cells, @p voxel edge, negative inside
 * solid) uploaded to an SSBO. Used as a shared FAR / per-chunk MEDIUM field the
 * gather ray marches once it leaves a finer field. Owns @p buf; free with
 * @ref lm_gpu_field_free. @p buf == 0 means "absent".
 */
typedef struct lm_gpu_field {
    uint32_t buf;        /**< GL SSBO name holding dims^3 floats (0 = absent). */
    int32_t  dims[3];    /**< Grid resolution. */
    float    origin[3];  /**< World-space minimum corner. */
    float    voxel;      /**< Cell edge (m). */
} lm_gpu_field_t;

/**
 * @brief Load GL 4.3 entry points via @p loader and compile the JFA + gather
 *        compute programs. Requires a current compute-capable context. Returns
 *        false on a missing entry point or a shader compile/link failure.
 */
bool lm_gpu_gather_init(const gl_loader_t *loader);

/** @brief Release the compute programs. NULL-safe. */
void lm_gpu_gather_shutdown(void);

/**
 * @brief Build a coarse JFA distance field over @p box (longest axis capped at
 *        @p max_dim cells, never finer than @p fine_voxel) by conservatively
 *        rasterising @p scene's triangles. On success fills @p out (caller owns
 *        @p out->buf; release with @ref lm_gpu_field_free). Needs a current
 *        compute context + @ref lm_gpu_gather_init. Returns false on failure.
 */
bool lm_gpu_field_build(const lm_mesh_scene_t *scene, float fine_voxel,
                        const phys_aabb_t *box, int max_dim, lm_gpu_field_t *out);

/** @brief Delete a field's GL buffer and zero it. NULL-safe. */
void lm_gpu_field_free(lm_gpu_field_t *field);

/**
 * @brief Run the full GPU gather over @p lm's luxels and write the per-luxel
 *        SH9 estimate into @p accum (3 coeff-sets per luxel, laid out as the CPU
 *        gather's accum). @p samples is the total per-luxel sample count for
 *        this call (stratified n x n, n = floor(sqrt(samples))).
 *
 *        Three trace levels: a fine NEAR field over @p region (or the whole SVO
 *        when @p region is NULL), a per-call MEDIUM field over a ~3x-region box
 *        (built internally when @p region is given), and an optional coarse
 *        shared FAR field @p far (built by the caller, e.g. one per chunk
 *        neighbourhood). A ray escapes to sky only after leaving all present
 *        fields. @p far may be NULL. Returns false on any GL/allocation failure
 *        or if @ref lm_gpu_gather_init was not run.
 */
bool lm_gpu_gather_run(const lm_lightmap_t *lm, lm_sh9_t *accum,
                       const npc_svo_grid_t *svo, const lm_mesh_scene_t *scene,
                       const phys_aabb_t *region, const lm_gpu_field_t *far,
                       const lm_light_t *lights, uint32_t n_lights,
                       const lm_sky_t *sky, float transition, float maxdist,
                       uint32_t samples, uint32_t bounces, uint32_t seed,
                       const char *sdf_out, int near_dim);

/**
 * @brief Chunked gather (rpg-fzht): partition @p scene_bounds into @p chunk_size
 *        cubic NEAR chunks (with @p margin overlap) and a coarser FAR grid whose
 *        cells each span a neighbourhood of near chunks. Each near chunk builds
 *        its OWN fine SVO over its outer box (from @p scene, at @p fine_voxel) so
 *        a massive scene never needs one whole-scene octree, and its luxels are
 *        gathered against that near SVO + near SDF, a per-chunk medium field, and
 *        the coarse FAR SDF shared by every chunk in the same far cell (built
 *        once per far cell). LM_FAR_MULT (far cell = mult x chunk), LM_FAR_DIM
 *        (far grid resolution) and LM_MED_MULT tune the hierarchy. Writes the
 *        same per-luxel @p accum as @ref lm_gpu_gather_run. Returns false on
 *        allocation / GPU failure.
 */
bool lm_gpu_gather_chunked(const lm_lightmap_t *lm, lm_sh9_t *accum,
                           phys_aabb_t scene_bounds, float fine_voxel,
                           const lm_mesh_scene_t *scene,
                           float chunk_size, float margin, const lm_light_t *lights,
                           uint32_t n_lights, const lm_sky_t *sky, float transition,
                           float maxdist, uint32_t samples, uint32_t bounces,
                           uint32_t seed, const char *sdf_prefix);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_GATHER_H */
