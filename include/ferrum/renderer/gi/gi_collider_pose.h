/**
 * @file gi_collider_pose.h
 * @brief Build posed dynamic-GI collider proxies for SDF injection (rpg-85as).
 *
 * Turns canonical collider primitives (fr_collider_prim_t, from either streaming
 * channel -- rpg-b5r3) into world-space gi_collider_t proxies positioned by the
 * owning body transform, or by a posed skeleton bone when the primitive is
 * bone-keyed (animated hierarchy). The resulting proxies are handed to
 * gi_runtime_frame each frame so moving/posed geometry occludes and bounces the
 * dynamic GI (and, later, is reusable for audio propagation over the same SDF).
 *
 * Pure math (no GL). Box/convex/mesh/compound become AABB box proxies (the
 * primitive's half_extents is the local bound); sphere/capsule are exact;
 * halfspace + point are skipped (static / volumeless).
 */
#ifndef FERRUM_RENDERER_GI_GI_COLLIDER_POSE_H
#define FERRUM_RENDERER_GI_GI_COLLIDER_POSE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "ferrum/renderer/gi/gi_sdf.h"    /* gi_collider_t */
#include "ferrum/asset/collider_prim.h"   /* fr_collider_prim_t */

/**
 * @brief Build world-space GI collider proxies from posed primitives.
 *
 * For each primitive the world transform is @p bone_xforms[prim->bone] when the
 * primitive is bone-keyed (and bone_xforms is non-NULL and in range), else
 * @p owner_xform; the primitive's local offset/rotation compose on top.
 *
 * @param prims       primitives to pose.
 * @param n_prims     count.
 * @param owner_xform 4x4 column-major body/object world transform (16 floats).
 * @param bone_xforms optional [n_bones*16] posed bone world transforms, or NULL.
 * @param n_bones     count for bone_xforms.
 * @param out         output proxies (caller storage).
 * @param out_cap     capacity of @p out (excess primitives are dropped).
 * @return number of proxies written (<= out_cap). Skipped kinds don't count.
 */
uint32_t gi_collider_pose_build(const fr_collider_prim_t *prims, uint32_t n_prims,
                                const float *owner_xform, const float *bone_xforms,
                                uint32_t n_bones, gi_collider_t *out,
                                uint32_t out_cap);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_RENDERER_GI_GI_COLLIDER_POSE_H */
