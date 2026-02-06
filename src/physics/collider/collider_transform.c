#include "ferrum/physics/collider.h"

#include <stddef.h>

#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

/**
 * Rotate a vector by a unit quaternion.
 * v' = v + 2w(q_xyz × v) + 2(q_xyz × (q_xyz × v))
 */
static phys_vec3_t quat_rotate_vec3(phys_quat_t q, phys_vec3_t v)
{
    phys_vec3_t u = {q.x, q.y, q.z};
    float w = q.w;
    phys_vec3_t uv = vec3_cross(u, v);
    phys_vec3_t uuv = vec3_cross(u, uv);
    uv = vec3_scale(uv, 2.0f * w);
    uuv = vec3_scale(uuv, 2.0f);
    return vec3_add(v, vec3_add(uv, uuv));
}

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
