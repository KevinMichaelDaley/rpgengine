/**
 * @file lm_svo_voxelize.h
 * @brief Voxelize surface material (diffuse reflectance + emission) into the SVO.
 *
 * The unified GI gather reads per-voxel material on a near hit, so the octree
 * must carry the surfaces' shading at voxel resolution -- and because that
 * shading comes from per-texel TEXTURES (not a flat material id), we sample the
 * albedo/emissive images of every triangle passing through each solid voxel at
 * the voxel's barycentric UV and average them into the leaf. The leaves are then
 * averaged up the octree (@ref lm_svo_mip_average_up) so a far cone reads a
 * coarse, pre-filtered material. This is finer-grained than splatting luxels,
 * which matters once the voxels are smaller than the lightmap texels.
 *
 * Requires the SVO already stamped SOLID for the same meshes. Writes each node's
 * diffuse/emissive. Ownership: borrows meshes + @p count scratch (node-count
 * sized). Offline (bake-time) only.
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
 * @brief Voxelize @p n_meshes meshes' material into @p svo: for every solid
 *        voxel a triangle covers, sample that triangle's albedo/emissive at the
 *        voxel's barycentric material-UV and average into the leaf; then average
 *        up the octree. Also writes the smooth (barycentric-interpolated) surface
 *        NORMAL per leaf into @p normal (node-count sized) so the gather shades
 *        near hits with the real surface normal instead of the blocky voxel face
 *        (which otherwise facets curved surfaces). @p count is node-count-sized
 *        scratch. Zeroes every node's diffuse/emissive and @p normal first.
 */
void lm_svo_voxelize(npc_svo_grid_t *svo, const lm_mesh_t *meshes,
                     uint32_t n_meshes, uint32_t *count, vec3_t *normal);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_LIGHTMAP_LM_SVO_VOXELIZE_H */
