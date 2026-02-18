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
#include "ferrum/physics/convex_hull.h"
#include "ferrum/physics/convex_compound.h"
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
            case PHYS_SHAPE_MESH: {
                const phys_mesh_shape_t *ms =
                    &args->meshes[collider->shape_index];
                if (ms->bvh.nodes && ms->bvh.node_count > 0
                    && ms->bvh.root < ms->bvh.node_count) {
                    phys_aabb_t root = ms->bvh.nodes[ms->bvh.root].bounds;
                    aabb->min = (phys_vec3_t){
                        root.min.x + center.x, root.min.y + center.y,
                        root.min.z + center.z};
                    aabb->max = (phys_vec3_t){
                        root.max.x + center.x, root.max.y + center.y,
                        root.max.z + center.z};
                } else {
                    *aabb = (phys_aabb_t){center, center};
                }
                break;
            }
            case PHYS_SHAPE_CONVEX: {
                const phys_convex_hull_t *hull =
                    &args->convex_hulls[collider->shape_index];
                *aabb = phys_convex_hull_world_aabb(hull, center, rotation);
                break;
            }
            case PHYS_SHAPE_COMPOUND: {
                const phys_convex_compound_t *cc =
                    &args->compounds[collider->shape_index];
                if (cc->child_count > 0) {
                    *aabb = phys_convex_hull_world_aabb(
                        &args->convex_hulls[cc->child_hull_indices[0]],
                        center, rotation);
                    for (uint32_t ci = 1; ci < cc->child_count; ci++) {
                        phys_aabb_t child_aabb = phys_convex_hull_world_aabb(
                            &args->convex_hulls[cc->child_hull_indices[ci]],
                            center, rotation);
                        phys_aabb_merge(aabb, aabb, &child_aabb);
                    }
                } else {
                    *aabb = (phys_aabb_t){center, center};
                }
                break;
            }
            case PHYS_SHAPE_HALFSPACE: {
                /* Halfspace is infinite — huge AABB for broadphase. */
                const float HS_EXTENT = 1.0e6f;
                aabb->min = (phys_vec3_t){
                    -HS_EXTENT + center.x,
                    -HS_EXTENT + center.y,
                    -HS_EXTENT + center.z};
                aabb->max = (phys_vec3_t){
                     HS_EXTENT + center.x,
                     HS_EXTENT + center.y,
                     HS_EXTENT + center.z};
                break;
            }
            default:
                /* Unknown shape — produce a zero-volume AABB at the center. */
                *aabb = (phys_aabb_t){center, center};
                break;
        }

        /* Never insert halfspaces into the spatial grid — they are
         * infinite and handled via a separate broadphase pass. */
        if (collider->type == PHYS_SHAPE_HALFSPACE) {
            continue;
        }

        /* Insert the body into the spatial grid unless we're excluding
         * static bodies (static geometry is handled via BVH). */
        if (!(args->exclude_static_from_grid && phys_body_is_static(body))) {
            phys_spatial_grid_insert(args->grid_out, i, aabb);
        }
    }
}
