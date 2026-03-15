/**
 * @file edit_entity_pivot.c
 * @brief Pivot-offset utility: compute geometry center from entity transform.
 *
 * Non-static functions (1 / 4 limit):
 *   edit_entity_geometry_center
 */

#include "ferrum/editor/edit_entity_pivot.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/quat.h"
#include "ferrum/math/vec3.h"

void edit_entity_geometry_center(const edit_entity_t *ent, float out[3]) {
    /* geometry_center = pos + R * diag(scale) * (-pivot_offset) */
    vec3_t scaled_neg_pivot = {
        -ent->pivot_offset[0] * ent->scale[0],
        -ent->pivot_offset[1] * ent->scale[1],
        -ent->pivot_offset[2] * ent->scale[2],
    };
    vec3_t world_offset = quat_rotate_vec3(ent->orientation, scaled_neg_pivot);
    out[0] = ent->pos[0] + world_offset.x;
    out[1] = ent->pos[1] + world_offset.y;
    out[2] = ent->pos[2] + world_offset.z;
}
