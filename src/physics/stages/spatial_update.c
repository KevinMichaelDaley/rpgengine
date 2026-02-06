/**
 * @file spatial_update.c
 * @brief Stage 2: Spatial Index Update implementation.
 *
 * Clears the spatial grid, computes world-space AABBs for all active
 * bodies based on their collider shape, and inserts each body into
 * the grid for broadphase collision detection.
 */

#include "ferrum/physics/spatial_update.h"

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/spatial_grid.h"

#include <stddef.h>

void phys_stage_spatial_update(const phys_spatial_update_args_t *args) {
    if (!args) {
        return;
    }

    phys_spatial_grid_clear(args->grid_out);

    for (uint32_t i = 0; i < args->body_count; ++i) {
        /* Skip inactive bodies when an active mask is provided. */
        if (args->active && !args->active[i]) {
            continue;
        }

        const phys_body_t *body = &args->bodies[i];
        const phys_collider_t *collider = &args->colliders[i];
        phys_aabb_t *aabb = &args->aabbs_out[i];

        /* Compute world-space center and rotation for this collider. */
        phys_vec3_t center = phys_collider_world_center(
            collider, body->position, body->orientation);
        phys_quat_t rotation = phys_collider_world_rotation(
            collider, body->orientation);

        /* Build the AABB based on the collider's shape type. */
        switch (collider->type) {
            case PHYS_SHAPE_SPHERE: {
                float radius = args->spheres[collider->shape_index].radius;
                phys_aabb_from_sphere(aabb, center, radius);
                break;
            }
            case PHYS_SHAPE_BOX: {
                phys_vec3_t half_extents =
                    args->boxes[collider->shape_index].half_extents;
                phys_aabb_from_box(aabb, center, rotation, half_extents);
                break;
            }
            case PHYS_SHAPE_CAPSULE: {
                const phys_capsule_t *cap =
                    &args->capsules[collider->shape_index];
                phys_aabb_from_capsule(
                    aabb, center, rotation, cap->radius, cap->half_height);
                break;
            }
            default:
                /* Unknown shape — produce a zero-volume AABB at the center. */
                *aabb = (phys_aabb_t){center, center};
                break;
        }

        /* Insert the body into the spatial grid. */
        phys_spatial_grid_insert(args->grid_out, i, aabb);
    }
}
