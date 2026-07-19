/**
 * @file server_level_load.c
 * @brief Descriptor collider set -> static physics bodies (rpg-q1cp).
 */
#include <stdint.h>

#include "ferrum/server/server_level_load.h"
#include "ferrum/physics/world.h"
#include "ferrum/physics/body.h"
#include "ferrum/scene/scene_desc.h"

static int has_bound(const scene_desc_collider_t *c)
{
    return c->half_extents[0] > 0.0f || c->half_extents[1] > 0.0f ||
           c->half_extents[2] > 0.0f;
}

uint32_t server_level_load_colliders(struct phys_world *world,
                                     const struct scene_desc *descp)
{
    if (world == NULL || descp == NULL) return 0u;
    const scene_desc_t *desc = descp;
    const phys_vec3_t zoff = { 0.0f, 0.0f, 0.0f };
    const phys_quat_t idq = { 0.0f, 0.0f, 0.0f, 1.0f };

    uint32_t created = 0;
    for (uint32_t i = 0; i < desc->collider_count; ++i) {
        const scene_desc_collider_t *c = &desc->colliders[i];

        /* Mesh/convex/compound without a bound have no usable geometry yet. */
        int is_geom = (c->kind == SCENE_DESC_COLLIDER_MESH ||
                       c->kind == SCENE_DESC_COLLIDER_CONVEX ||
                       c->kind == SCENE_DESC_COLLIDER_COMPOUND);
        if (is_geom && !has_bound(c)) continue;

        uint32_t bi = phys_world_create_body(world);
        if (bi == UINT32_MAX) break;   /* pool full. */
        phys_body_t *b = phys_world_get_body(world, bi);
        if (b == NULL) continue;

        b->position = (phys_vec3_t){ c->position[0], c->position[1], c->position[2] };
        b->orientation = (phys_quat_t){ c->rotation[0], c->rotation[1],
                                        c->rotation[2], c->rotation[3] };
        phys_body_set_mass(b, 0.0f);   /* static (inv_mass 0). */
        b->flags |= PHYS_BODY_FLAG_STATIC;

        phys_vec3_t he = { c->half_extents[0], c->half_extents[1], c->half_extents[2] };
        switch (c->kind) {
        case SCENE_DESC_COLLIDER_BOX:
            phys_world_set_box_collider(world, bi, he, zoff, idq);
            break;
        case SCENE_DESC_COLLIDER_SPHERE:
            phys_world_set_sphere_collider(world, bi, c->radius, zoff);
            break;
        case SCENE_DESC_COLLIDER_CAPSULE:
            phys_world_set_capsule_collider(world, bi, c->radius, c->half_height,
                                            zoff, idq);
            break;
        case SCENE_DESC_COLLIDER_HALFSPACE:
            phys_world_set_halfspace_collider(world, bi,
                (phys_vec3_t){ c->normal[0], c->normal[1], c->normal[2] },
                c->plane_offset);
            break;
        case SCENE_DESC_COLLIDER_POINT:
            phys_world_set_point_collider(world, bi, zoff);
            break;
        case SCENE_DESC_COLLIDER_MESH:
        case SCENE_DESC_COLLIDER_CONVEX:
        case SCENE_DESC_COLLIDER_COMPOUND:
        default:
            /* Coarse AABB box proxy from the descriptor bound; precise triangle-
             * mesh collision (load geometry + build BVH) is a documented follow-on. */
            phys_world_set_box_collider(world, bi, he, zoff, idq);
            break;
        }
        ++created;
    }
    return created;
}
