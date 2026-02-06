/**
 * @file aabb_update.c
 * @brief Stage 4: AABB Update implementation.
 *
 * Computes world-space AABBs for bodies in active tiers (T0-T4),
 * skipping T5 (sleeping). Uses the same AABB computation logic as
 * spatial_update but is driven by tier lists.
 */

#include "ferrum/physics/aabb_update.h"

#include "ferrum/physics/aabb.h"
#include "ferrum/physics/body.h"
#include "ferrum/physics/collider.h"
#include "ferrum/physics/tier_list.h"

#include <stddef.h>

void phys_stage_aabb_update(const phys_aabb_update_args_t *args) {
    if (!args || !args->tier_lists) {
        return;
    }

    /* Iterate active tiers T0-T4, skipping T5 (sleeping). */
    for (int tier = PHYS_TIER_0_DIRECT; tier < PHYS_TIER_5_SLEEPING; ++tier) {
        const phys_tier_list_t *list = &args->tier_lists->tiers[tier];
        if (!list->indices) {
            continue;
        }

        for (uint32_t j = 0; j < list->count; ++j) {
            uint32_t body_idx = list->indices[j];
            const phys_body_t *body = &args->bodies[body_idx];
            const phys_collider_t *collider = &args->colliders[body_idx];
            phys_aabb_t *aabb = &args->aabbs_out[body_idx];

            /* Compute world-space center and rotation for this collider. */
            phys_vec3_t center = phys_collider_world_center(
                collider, body->position, body->orientation);
            phys_quat_t rotation = phys_collider_world_rotation(
                collider, body->orientation);

            /* Build the AABB based on the collider's shape type. */
            switch (collider->type) {
                case PHYS_SHAPE_SPHERE: {
                    float radius =
                        args->spheres[collider->shape_index].radius;
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
                    /* Unknown shape — zero-volume AABB at center. */
                    *aabb = (phys_aabb_t){center, center};
                    break;
            }
        }
    }
}
