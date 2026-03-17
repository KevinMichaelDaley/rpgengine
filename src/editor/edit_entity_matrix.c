/**
 * @file edit_entity_matrix.c
 * @brief Compute model matrix for editor entities.
 *
 * Builds T(pos) * R(quat) * S(scale) * T(-pivot) model matrix.
 * Extracted from scene_viewport_draw.c so snap raycasting and other
 * systems can compute entity model matrices without depending on
 * the viewport draw module.
 *
 * Non-static functions (1 / 4 limit):
 *   edit_entity_build_model_matrix
 */

#include "ferrum/editor/edit_entity_matrix.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

mat4_t edit_entity_build_model_matrix(const struct edit_entity *ent) {
    if (!ent) return mat4_identity();

    mat4_t pivot_shift = mat4_translation(
        -ent->pivot_offset[0], -ent->pivot_offset[1],
        -ent->pivot_offset[2]);
    mat4_t scale = mat4_scaling(ent->scale[0], ent->scale[1], ent->scale[2]);
    mat4_t rot;
    quat_to_mat4(ent->orientation, &rot);
    mat4_t trans = mat4_translation(ent->pos[0], ent->pos[1], ent->pos[2]);

    /* T(pos) * R * S * T(-pivot_offset) */
    return mat4_mul(trans, mat4_mul(rot, mat4_mul(scale, pivot_shift)));
}
