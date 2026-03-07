#include "ferrum/physics/collider.h"

#include <stddef.h>

void phys_collider_init_sphere(phys_collider_t *c,
                               uint32_t sphere_idx,
                               phys_vec3_t offset)
{
    if (!c) { return; }

    *c = (phys_collider_t){0};
    c->type = PHYS_SHAPE_SPHERE;
    c->shape_index = sphere_idx;
    c->local_offset = offset;
    c->local_rotation = (phys_quat_t){0, 0, 0, 1};
}

void phys_collider_init_box(phys_collider_t *c,
                            uint32_t box_idx,
                            phys_vec3_t offset,
                            phys_quat_t rotation)
{
    if (!c) { return; }

    *c = (phys_collider_t){0};
    c->type = PHYS_SHAPE_BOX;
    c->shape_index = box_idx;
    c->local_offset = offset;
    c->local_rotation = rotation;
}

void phys_collider_init_capsule(phys_collider_t *c,
                                uint32_t capsule_idx,
                                phys_vec3_t offset,
                                phys_quat_t rotation)
{
    if (!c) { return; }

    *c = (phys_collider_t){0};
    c->type = PHYS_SHAPE_CAPSULE;
    c->shape_index = capsule_idx;
    c->local_offset = offset;
    c->local_rotation = rotation;
}

void phys_collider_init_mesh(phys_collider_t *c,
                              uint32_t mesh_idx,
                              phys_vec3_t offset)
{
    if (!c) { return; }

    *c = (phys_collider_t){0};
    c->type = PHYS_SHAPE_MESH;
    c->shape_index = mesh_idx;
    c->local_offset = offset;
    c->local_rotation = (phys_quat_t){0, 0, 0, 1};
}

void phys_collider_init_halfspace(phys_collider_t *c,
                                   uint32_t halfspace_idx,
                                   phys_vec3_t offset)
{
    if (!c) { return; }

    *c = (phys_collider_t){0};
    c->type = PHYS_SHAPE_HALFSPACE;
    c->shape_index = halfspace_idx;
    c->local_offset = offset;
    c->local_rotation = (phys_quat_t){0, 0, 0, 1};
}

void phys_collider_init_convex(phys_collider_t *c,
                               uint32_t convex_idx,
                               phys_vec3_t offset,
                               phys_quat_t rotation)
{
    if (!c) { return; }

    *c = (phys_collider_t){0};
    c->type = PHYS_SHAPE_CONVEX;
    c->shape_index = convex_idx;
    c->local_offset = offset;
    c->local_rotation = rotation;
}
