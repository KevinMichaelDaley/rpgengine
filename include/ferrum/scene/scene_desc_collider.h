/**
 * @file scene_desc_collider.h
 * @brief Physics collider-set entry in a scene/level descriptor (rpg-51nf).
 *
 * The level's authoritative collision geometry: primitives (box/sphere/capsule/
 * halfspace) and mesh colliders. The server loads these into the physics world;
 * they are also streamed to the client so client-side prediction can run real
 * collision (rpg-b5r3). Pure data, no physics/GL dependency.
 */
#ifndef FERRUM_SCENE_SCENE_DESC_COLLIDER_H
#define FERRUM_SCENE_SCENE_DESC_COLLIDER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/scene/scene_desc_object.h" /* SCENE_DESC_OBJ_NAME_CAP, SCENE_DESC_PATH_CAP */

/** Collider shape kinds (mirror the full physics primitive set). */
typedef enum scene_desc_collider_kind {
    SCENE_DESC_COLLIDER_BOX       = 0, /**< half_extents. */
    SCENE_DESC_COLLIDER_SPHERE    = 1, /**< radius. */
    SCENE_DESC_COLLIDER_CAPSULE   = 2, /**< radius + half_height (local Y axis). */
    SCENE_DESC_COLLIDER_HALFSPACE = 3, /**< normal + plane_offset. */
    SCENE_DESC_COLLIDER_MESH      = 4, /**< mesh[] triangle collision asset. */
    SCENE_DESC_COLLIDER_CONVEX    = 5, /**< mesh[] convex-hull point asset. */
    SCENE_DESC_COLLIDER_COMPOUND  = 6, /**< mesh[] convex-decomposition asset. */
    SCENE_DESC_COLLIDER_POINT     = 7, /**< offset-only point collider. */
} scene_desc_collider_kind_t;

/**
 * @brief One physics collider in the level's collision set.
 *
 * Only the fields relevant to @c kind are meaningful; the rest are zero. @c mesh
 * is used only for SCENE_DESC_COLLIDER_MESH (path to a collision mesh asset, may
 * reuse a render mesh). @c object_ref indexes the owning scene_desc_t::objects
 * entry, or -1 for a standalone collider. @c is_static maps to inv_mass = 0.
 */
typedef struct scene_desc_collider {
    scene_desc_collider_kind_t kind;
    char     name[SCENE_DESC_OBJ_NAME_CAP];
    float    position[3];       /**< world translation. */
    float    rotation[4];       /**< orientation quaternion (x,y,z,w). */
    float    half_extents[3];   /**< BOX. */
    float    radius;            /**< SPHERE / CAPSULE. */
    float    half_height;       /**< CAPSULE (segment half-length along local Y). */
    float    normal[3];         /**< HALFSPACE plane normal. */
    float    plane_offset;      /**< HALFSPACE signed distance along normal. */
    char     mesh[SCENE_DESC_PATH_CAP]; /**< MESH/CONVEX/COMPOUND geometry asset path. */
    uint64_t geom_asset;        /**< resolved streamed geometry asset id (0 = by path). */
    int      solid;             /**< MESH: 1 = solid. */
    int32_t  object_ref;        /**< owning object index, or -1 (standalone). */
    int32_t  bone;              /**< bone index this collider is keyed to (-1 = root). */
    bool     is_static;         /**< true => inv_mass 0 (immovable). */
} scene_desc_collider_t;

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SCENE_SCENE_DESC_COLLIDER_H */
