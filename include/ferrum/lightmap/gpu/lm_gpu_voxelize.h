/**
 * @file lm_gpu_voxelize.h
 * @brief Bake-side GPU voxel rasterizer (rpg-bpiz): full rasterization into a
 *        sliced render target, replacing the CPU dense stamping / surface
 *        subsampling loops of the offline bake.
 *
 * Mechanism (mirrors the runtime dynamic-object volume rasterizer,
 * gi_voxelize_draw.c): an attachment-less FBO sized to the volume's largest
 * cross-section rasterizes the bake meshes; the destination volume is bound as
 * a LAYERED r32ui image (the whole 3D texture is the sliced render target) and
 * the fragment shader writes the voxel derived from the fragment's world
 * position with imageStore/imageAtomic* -- no per-slice FBO attachments, no
 * gl_Layer routing. Channels (occupancy, area, albedo, emissive, normal,
 * transmission) live as fixed-point z-plane slabs of one r32ui volume so a
 * single pass accumulates every output; large volumes are processed in z-slabs
 * to bound GPU memory.
 *
 * Semantics match the CPU voxelizers it replaces:
 *  - occupancy: any covering fragment from any projection (hole-free union);
 *  - albedo: area-weighted MEAN reflectance (fragments weighted by their true
 *    surface footprint, dominant-projection only so surfaces count once);
 *  - emissive: area-weighted SUM over the voxel cross-section (emitters add);
 *  - normal: renormalised area-weighted mean;
 *  - transmission: MIN over every stamping surface (opaque wins).
 *
 * GL: needs a current GL 4.3 context; entry points load via @ref gl_loader_t
 * (the lightmap library stays headless -- no glad). State is file-static
 * (offline, single-threaded bake use).
 *
 * Ownership: @ref lm_gpu_voxelize_run mallocs every array in the output grid;
 * release with @ref lm_gpu_vox_grid_free. Error semantics: all entry points
 * return false / no-op on NULL or invalid arguments and on any GL failure;
 * running before init returns false.
 */
#ifndef FERRUM_LIGHTMAP_GPU_LM_GPU_VOXELIZE_H
#define FERRUM_LIGHTMAP_GPU_LM_GPU_VOXELIZE_H

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/physics/aabb.h"
#include "ferrum/renderer/gl_loader.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief A dense voxelization of a world-space box: dims[0]*dims[1]*dims[2]
 *        cells, x-major then y then z (index = (z*dims[1]+y)*dims[0]+x).
 *        Every array is owned by the grid (malloc'd by the run, freed by
 *        @ref lm_gpu_vox_grid_free); cells no surface touched hold zeros
 *        (transmission 1.0).
 */
typedef struct lm_gpu_vox_grid {
    int32_t   dims[3];    /**< Cell counts per axis (>= 1). */
    float     origin[3];  /**< World minimum corner of the box. */
    float     cell[3];    /**< Per-axis cell edge (m). */
    uint32_t *occ;        /**< n cells: 1 = surface voxel, else 0. */
    float    *area;       /**< n: accumulated surface area (m^2 weight). */
    float    *albedo;     /**< n*3: area-weighted mean RGB (0 where area==0). */
    float    *emissive;   /**< n*3: emitted RGB, summed over the cross-section. */
    float    *normal;     /**< n*3: renormalised mean normal (0 where area==0). */
    float    *trans;      /**< n: transmission (1 clear .. 0 opaque), MIN. */
} lm_gpu_vox_grid_t;

/**
 * @brief Load GL entry points via @p loader and build the raster + clear
 *        programs and the attachment-less FBO. Requires a current GL 4.3
 *        context. Returns false on a missing entry point or shader failure.
 */
bool lm_gpu_voxelize_init(const gl_loader_t *loader);

/** @brief Release programs/FBO. NULL-context safe; idempotent. */
void lm_gpu_voxelize_shutdown(void);

/**
 * @brief Voxelize @p meshes (world-space lm meshes; meshes whose bounds miss
 *        @p box are culled) over @p box at @p dims cells per axis, filling
 *        @p out. Returns false before init, on invalid arguments (@p box /
 *        @p dims / @p out NULL, dims < 1, meshes NULL with n > 0) or on GL /
 *        allocation failure; @p out is untouched on failure.
 */
bool lm_gpu_voxelize_run(const lm_mesh_t *meshes, uint32_t n_meshes,
                         const phys_aabb_t *box, const int dims[3],
                         lm_gpu_vox_grid_t *out);

/** @brief Free every array of @p g and zero it. NULL-safe. */
void lm_gpu_vox_grid_free(lm_gpu_vox_grid_t *g);

/**
 * @brief Voxelize @p box at FULL resolution (@p dims unbounded -- processed
 *        in cubic tiles internally) and sample the result at @p n_points
 *        world positions (@p points, xyz triples): readback scales with the
 *        point count, never the grid volume. Fills, per point: @p out_area
 *        (accumulated surface area in its cell), @p out_albedo (n*3
 *        area-mean reflectance, 0 where no area) and @p out_emissive (n*3,
 *        per voxel cross-section). Tiles containing no points are skipped.
 *        Returns false before init, on invalid arguments or GL/allocation
 *        failure; outputs are unspecified on failure.
 */
bool lm_gpu_voxelize_sample(const lm_mesh_t *meshes, uint32_t n_meshes,
                            const phys_aabb_t *box, const int dims[3],
                            const float *points, uint32_t n_points,
                            float *out_area, float *out_albedo,
                            float *out_emissive);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_GPU_LM_GPU_VOXELIZE_H */
