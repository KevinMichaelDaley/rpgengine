/**
 * @file collider_prim.h
 * @brief Canonical collider-primitive schema (rpg-b5r3).
 *
 * The one representation of a physics collider that BOTH streaming channels
 * produce, so the dynamic-SDF-injection adapter (rpg-85as) consumes a single
 * type regardless of where a dynamic object came from:
 *   - server-spawned bodies -> from net_repl_body_spawn (network channel),
 *   - level-authored objects -> from scene_desc_collider (asset/descriptor
 *     channel, incl. dynamic objects that are NOT server-spawned).
 * Covers the full physics collider set (see phys_shape_type_t). Mesh/convex/
 * compound geometry is referenced by a streamed asset id (@c geom_asset), which
 * the asset streamer pages. Pure data, no GL / no physics dependency.
 */
#ifndef FERRUM_ASSET_COLLIDER_PRIM_H
#define FERRUM_ASSET_COLLIDER_PRIM_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/** Collider shape kinds (mirror phys_shape_type_t, incl. convex + compound). */
typedef enum fr_collider_prim_kind {
    FR_COLLIDER_PRIM_BOX       = 0, /**< half_extents. */
    FR_COLLIDER_PRIM_SPHERE    = 1, /**< radius. */
    FR_COLLIDER_PRIM_CAPSULE   = 2, /**< radius + half_height (local Y). */
    FR_COLLIDER_PRIM_HALFSPACE = 3, /**< normal + plane_offset. */
    FR_COLLIDER_PRIM_MESH      = 4, /**< geom_asset triangles (+ solid). */
    FR_COLLIDER_PRIM_CONVEX    = 5, /**< geom_asset hull points. */
    FR_COLLIDER_PRIM_COMPOUND  = 6, /**< geom_asset convex decomposition. */
    FR_COLLIDER_PRIM_POINT     = 7, /**< offset only. */
    FR_COLLIDER_PRIM_KIND_COUNT
} fr_collider_prim_kind_t;

/**
 * @brief A single collider primitive.
 *
 * Only the fields relevant to @c kind are meaningful. @c offset / @c rotation
 * are the collider's transform relative to its owning body/object. @c geom_asset
 * is the streamed geometry asset id for MESH/CONVEX/COMPOUND (0 = none/inline).
 */
typedef struct fr_collider_prim {
    fr_collider_prim_kind_t kind;
    int32_t  bone;             /**< skeleton bone this collider is keyed to
                                *   (-1 = owner root); when >=0 the collider
                                *   follows that posed bone and offset/rotation
                                *   are relative to the bone (animated hierarchy). */
    float    offset[3];        /**< local translation from the owner/bone origin. */
    float    rotation[4];      /**< local orientation quaternion (x,y,z,w). */
    float    half_extents[3];  /**< BOX. */
    float    radius;           /**< SPHERE / CAPSULE. */
    float    half_height;      /**< CAPSULE (segment half-length, local Y). */
    float    normal[3];        /**< HALFSPACE plane normal. */
    float    plane_offset;     /**< HALFSPACE signed distance along normal. */
    uint64_t geom_asset;       /**< MESH/CONVEX/COMPOUND streamed geometry id. */
    int      solid;            /**< MESH: 1 = solid (inside is filled). */
} fr_collider_prim_t;

struct net_repl_body_spawn; /* ferrum/net/replication/body_spawn.h */
struct scene_desc_collider; /* ferrum/scene/scene_desc_collider.h */

/**
 * @brief Build a canonical primitive from a decoded BODY_SPAWN (network channel).
 *
 * Uses the BODY_SPAWN shape conventions (box: half_x/y/z; sphere: radius=half_x;
 * capsule: radius=half_x, half_height=half_y; halfspace: normal=half_x/y/z,
 * distance=off_x). Mesh/convex/compound geometry rides the MESH_DATA transport
 * keyed by body_id, so @c geom_asset is set to @p spawn->body_id.
 */
void fr_collider_prim_from_body_spawn(const struct net_repl_body_spawn *spawn,
                                      fr_collider_prim_t *out);

/**
 * @brief Build a canonical primitive from a scene-descriptor collider (asset
 *        channel). Straight field mapping; mesh/convex/compound geometry id is
 *        @p desc->geom_asset (0 when only a mesh path is given).
 */
void fr_collider_prim_from_desc(const struct scene_desc_collider *desc,
                                fr_collider_prim_t *out);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ASSET_COLLIDER_PRIM_H */
