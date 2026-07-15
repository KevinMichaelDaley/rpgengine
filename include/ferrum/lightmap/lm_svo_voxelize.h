/**
 * @file lm_svo_voxelize.h
 * @brief Voxelize surface material (diffuse reflectance + emission) into the SVO.
 *
 * The unified GI gather reads per-voxel material on a near hit, so the octree
 * must carry the surfaces' shading at voxel resolution. Because triangles can be
 * huge (a whole wall is two triangles) we SUBSAMPLE each triangle's surface on a
 * uniform grid finer than a voxel -- treating each subsample as the centre of a
 * disk of area pi*(step/2)^2 -- and scatter its texture-sampled material,
 * area-weighted, into the solid voxels it covers: diffuse as an area-weighted
 * MEAN reflectance, emissive as an area-weighted SUM of radiance over the voxel
 * cross-section (so co-located emitters add and non-emissive geometry never
 * dilutes emission). The leaves are then averaged up the octree (@ref
 * lm_svo_mip_average_up) so a far cone reads a coarse, pre-filtered material.
 *
 * Requires the SVO already stamped SOLID for the same meshes. Writes each node's
 * diffuse/emissive. Ownership: borrows meshes + @p area scratch (node-count
 * sized, holds the accumulated surface area per voxel). Offline (bake) only.
 */
#ifndef FERRUM_LIGHTMAP_LM_SVO_VOXELIZE_H
#define FERRUM_LIGHTMAP_LM_SVO_VOXELIZE_H

#include <stdint.h>

#include "ferrum/lightmap/lm_mesh.h"
#include "ferrum/npc/npc_svo.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Voxelize @p n_meshes meshes' material into @p svo by subsampling each
 *        triangle's surface (disk-area weighted) and scattering the texture
 *        material into the solid voxels it covers; then average up the octree.
 *        Also writes the smooth (area-weighted) surface NORMAL per leaf into
 *        @p normal (node-count sized) so the gather shades near hits with the
 *        real surface normal instead of the blocky voxel face. @p area is
 *        node-count-sized scratch (accumulated surface area per voxel). Zeroes
 *        every node's diffuse/emissive and @p normal / @p area first.
 */
void lm_svo_voxelize(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                     uint32_t n_meshes, float *area, vec3_t *normal);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SVO_VOXELIZE_H */
