/**
 * @file snap_surface_apply_offset.c
 * @brief Apply surface snap with AABB normal offset.
 *
 * Positions the entity so it rests on the surface: hit_position +
 * normal * dot(half_extents, abs(normal)). Also orients entity
 * so local +Y aligns with face normal.
 *
 * Non-static functions (1 / 4 limit):
 *   snap_apply_on_surface
 */

#include "ferrum/editor/viewport/snap/snap_surface_apply.h"
#include "ferrum/editor/viewport/snap/snap_raycast.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/mat4.h"

#include <math.h>

/**
 * @brief Build an orientation quaternion that maps local +Y to the
 *        given world-space normal.
 */
static quat_t orient_y_to_normal_(vec3_t normal) {
    vec3_t hint = {0, 1, 0};
    float dot = fabsf(normal.x * hint.x + normal.y * hint.y +
                       normal.z * hint.z);
    if (dot > 0.99f) {
        hint = (vec3_t){0, 0, 1};
    }

    vec3_t new_x = vec3_cross(hint, normal);
    float len_x = sqrtf(new_x.x * new_x.x + new_x.y * new_x.y +
                          new_x.z * new_x.z);
    if (len_x < 1e-6f) {
        return (quat_t){0, 0, 0, 1};
    }
    new_x.x /= len_x;
    new_x.y /= len_x;
    new_x.z /= len_x;

    vec3_t new_z = vec3_cross(new_x, normal);

    mat4_t rot_mat;
    rot_mat.m[0]  = new_x.x;  rot_mat.m[1]  = new_x.y;  rot_mat.m[2]  = new_x.z;  rot_mat.m[3]  = 0;
    rot_mat.m[4]  = normal.x;  rot_mat.m[5]  = normal.y;  rot_mat.m[6]  = normal.z;  rot_mat.m[7]  = 0;
    rot_mat.m[8]  = new_z.x;  rot_mat.m[9]  = new_z.y;  rot_mat.m[10] = new_z.z;  rot_mat.m[11] = 0;
    rot_mat.m[12] = 0;        rot_mat.m[13] = 0;         rot_mat.m[14] = 0;         rot_mat.m[15] = 1;

    return quat_from_mat4(&rot_mat);
}

void snap_apply_on_surface(struct edit_entity *ent,
                             const struct snap_hit *hit,
                             vec3_t half_extents) {
    if (!ent || !hit || !hit->valid) return;

    /* Compute offset along normal: dot(half_extents, abs(normal)). */
    float offset = half_extents.x * fabsf(hit->normal.x) +
                   half_extents.y * fabsf(hit->normal.y) +
                   half_extents.z * fabsf(hit->normal.z);

    ent->pos[0] = hit->position.x + hit->normal.x * offset;
    ent->pos[1] = hit->position.y + hit->normal.y * offset;
    ent->pos[2] = hit->position.z + hit->normal.z * offset;

    /* Orient entity so local +Y aligns with face normal. */
    ent->orientation = orient_y_to_normal_(hit->normal);
}
