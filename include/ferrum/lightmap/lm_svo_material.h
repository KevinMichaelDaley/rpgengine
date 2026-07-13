/**
 * @file lm_svo_material.h
 * @brief Bake prepass: rasterize scene geometry into the SVO while stamping each
 *        solid voxel with the material id of the surface that created it.
 *
 * The navigation rasteriser (@ref npc_svo_rasterize_triangle) only marks
 * occupancy and leaves every voxel's material at its default. The lightmap
 * baker's far-field pass (@ref lm_farfield_gather) shades distant reflectors and
 * emitters from the voxel material, so those voxels must carry the REAL material
 * of their surface. These functions rasterize a triangle/mesh and then set the
 * material id on every solid leaf the geometry occupies, so a later material
 * lookup returns that surface's true albedo/emissive.
 *
 * Ownership: none -- mutates @p svo in place. Nullability: pointers non-NULL.
 * Errors: none (out-of-bounds geometry is clipped by the rasteriser). Later
 * writers win where surfaces of different materials share a voxel. Offline.
 */
#ifndef FERRUM_LIGHTMAP_LM_SVO_MATERIAL_H
#define FERRUM_LIGHTMAP_LM_SVO_MATERIAL_H

#include <stdint.h>

#include "ferrum/npc/npc_svo.h"
#include "ferrum/physics/mesh_collider.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Rasterize @p tri into @p svo (marking solid) and stamp @p material_id
 *        onto every solid leaf the triangle occupies.
 */
void lm_svo_stamp_triangle(npc_svo_grid_t *svo, const phys_triangle_t *tri,
                           uint16_t material_id);

/**
 * @brief Rasterize and stamp @p count triangles of one surface with
 *        @p material_id (see @ref lm_svo_stamp_triangle).
 */
void lm_svo_stamp_mesh(npc_svo_grid_t *svo, const phys_triangle_t *tris,
                       uint32_t count, uint16_t material_id);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SVO_MATERIAL_H */
