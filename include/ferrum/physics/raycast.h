#ifndef FERRUM_PHYSICS_RAYCAST_H
#define FERRUM_PHYSICS_RAYCAST_H

/** @file
 * @brief Phase 5.1: Raycasts against primitive collider shapes.
 *
 * Provides world queries for gameplay (hitscan, line of sight, etc).
 *
 * The ray direction is expected to be normalized; distance units are in
 * world-space meters.
 */

#include <stdbool.h>
#include <stdint.h>

#include "ferrum/physics/phys_vec3.h"

struct phys_world;

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Ray query definition. */
typedef struct phys_ray {
    phys_vec3_t origin;
    phys_vec3_t direction;   /**< Normalized direction (non-zero). */
    float max_distance;      /**< Maximum distance; <= 0 yields no hit. */
} phys_ray_t;

/** @brief Raycast hit result. */
typedef struct phys_raycast_hit {
    float distance;          /**< Distance along the ray to the hit point. */
    phys_vec3_t point;       /**< World-space hit point. */
    phys_vec3_t normal;      /**< World-space outward normal at the hit point. */
    uint32_t body_id;        /**< World body index that was hit. */
} phys_raycast_hit_t;

/**
 * @brief Raycast against all active bodies in @p world.
 *
 * @param world      World to query (NULL returns false).
 * @param ray        Ray definition (NULL returns false).
 * @param hit        Output hit (may be NULL; function still returns true/false).
 * @param layer_mask Bitmask filter; a body is considered only when
 *                   (layer_mask & (1u << collider.layer_id)) != 0.
 * @return true if any body was hit; false otherwise.
 *
 * Ownership: borrows all inputs; does not allocate heap memory.
 */
bool phys_raycast(const struct phys_world *world, const phys_ray_t *ray,
                  phys_raycast_hit_t *hit, uint32_t layer_mask);

/**
 * @brief Raycast and collect up to @p max_hits hits.
 *
 * Hits are sorted by increasing distance.
 *
 * @param world      World to query (NULL returns 0).
 * @param ray        Ray definition (NULL returns 0).
 * @param hits       Output hit array (NULL returns 0).
 * @param max_hits   Capacity of @p hits.
 * @param layer_mask Layer filter mask (see phys_raycast).
 * @return Number of hits written to @p hits.
 */
uint32_t phys_raycast_all(const struct phys_world *world, const phys_ray_t *ray,
                          phys_raycast_hit_t *hits, uint32_t max_hits,
                          uint32_t layer_mask);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* FERRUM_PHYSICS_RAYCAST_H */
