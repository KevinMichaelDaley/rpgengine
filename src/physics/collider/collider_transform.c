#include "ferrum/physics/collider.h"

#include <stddef.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

phys_vec3_t phys_collider_world_center(const phys_collider_t *c,
                                       phys_vec3_t body_pos,
                                       phys_quat_t body_rot)
{
    if (!c) { return body_pos; }

    phys_vec3_t rotated_offset = quat_rotate_vec3(body_rot, c->local_offset);
    return vec3_add(body_pos, rotated_offset);
}

phys_quat_t phys_collider_world_rotation(const phys_collider_t *c,
                                         phys_quat_t body_rot)
{
    if (!c) { return body_rot; }

    return quat_mul(body_rot, c->local_rotation);
}
