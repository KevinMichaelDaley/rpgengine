/**
 * @file server_level_load.h
 * @brief Load a level's collision geometry from a scene descriptor into the
 *        server physics world (rpg-q1cp).
 *
 * Replaces the editor round-trip: each collider in the descriptor's collider set
 * becomes a static physics body (inv_mass 0) with the matching collider attached,
 * so the server boots straight from a .scene into a populated, authoritative
 * physics world. Headless (no GL). Primitive colliders (box/sphere/capsule/
 * halfspace/point) map exactly; mesh/convex/compound become a coarse AABB box
 * proxy from the descriptor's half_extents bound (precise triangle-mesh collision
 * -- loading geometry + building a BVH -- is a documented follow-on).
 */
#ifndef FERRUM_SERVER_SERVER_LEVEL_LOAD_H
#define FERRUM_SERVER_SERVER_LEVEL_LOAD_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

struct phys_world;   /* ferrum/physics/world.h */
struct scene_desc;   /* ferrum/scene/scene_desc.h */

/**
 * @brief Create static physics bodies for a descriptor's collider set.
 *
 * @param world  physics world to populate (must be initialized).
 * @param desc   parsed scene descriptor.
 * @return number of collider bodies created.
 *
 * A mesh/convex/compound collider with no half_extents bound is skipped (no body
 * created). Point colliders attach a point collider. All created bodies are
 * static.
 */
uint32_t server_level_load_colliders(struct phys_world *world,
                                     const struct scene_desc *desc);

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_SERVER_SERVER_LEVEL_LOAD_H */
