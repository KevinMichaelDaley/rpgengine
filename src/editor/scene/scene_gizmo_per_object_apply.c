/**
 * @file scene_gizmo_per_object_apply.c
 * @brief Per-object gizmo drag and rotation application.
 *
 * Applies transform deltas to individual entities rather than the
 * entire selection. Used when per-object gizmo mode is active.
 *
 * Non-static functions (2 / 4 limit):
 *   per_object_gizmo_apply_drag
 *   per_object_gizmo_apply_rotate
 */

#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/quat.h"

void per_object_gizmo_apply_drag(
    edit_entity_store_t *entities,
    uint32_t entity_id,
    gizmo_mode_t mode,
    vec3_t delta)
{
    if (!entities) return;

    edit_entity_t *ent = edit_entity_store_get_mut(entities, entity_id);
    if (!ent) return;

    switch (mode) {
    case GIZMO_MODE_TRANSLATE:
        ent->pos[0] += delta.x;
        ent->pos[1] += delta.y;
        ent->pos[2] += delta.z;
        break;
    case GIZMO_MODE_SCALE:
        ent->scale[0] *= (1.0f + delta.x);
        ent->scale[1] *= (1.0f + delta.y);
        ent->scale[2] *= (1.0f + delta.z);
        break;
    default:
        break;
    }
}

void per_object_gizmo_apply_rotate(
    edit_entity_store_t *entities,
    uint32_t entity_id,
    quat_t dq,
    const vec3_t *pivot)
{
    if (!entities) return;

    edit_entity_t *ent = edit_entity_store_get_mut(entities, entity_id);
    if (!ent) return;

    /* Compose quaternion rotation. */
    ent->orientation = quat_normalize_safe(
        quat_mul(dq, ent->orientation), 1e-8f);

    /* Sync euler cache for display. Canonicalize w >= 0 to avoid
     * branch jumps in the euler decomposition. */
    quat_t cq = ent->orientation;
    if (cq.w < 0.0f) {
        cq.x = -cq.x; cq.y = -cq.y;
        cq.z = -cq.z; cq.w = -cq.w;
    }
    quat_to_euler_yxz(cq, &ent->rot[0], &ent->rot[1], &ent->rot[2]);
    {
        float r2d = 180.0f / 3.14159265358979323846f;
        ent->rot[0] *= r2d;
        ent->rot[1] *= r2d;
        ent->rot[2] *= r2d;
    }

    /* If a pivot is provided (cursor basis), orbit entity position
     * around the pivot point using the same rotation. */
    if (pivot) {
        mat4_t rot_mat;
        quat_to_mat4(dq, &rot_mat);
        float ox = ent->pos[0] - pivot->x;
        float oy = ent->pos[1] - pivot->y;
        float oz = ent->pos[2] - pivot->z;
        float nx = rot_mat.m[0] * ox + rot_mat.m[4] * oy
                  + rot_mat.m[8]  * oz;
        float ny = rot_mat.m[1] * ox + rot_mat.m[5] * oy
                  + rot_mat.m[9]  * oz;
        float nz = rot_mat.m[2] * ox + rot_mat.m[6] * oy
                  + rot_mat.m[10] * oz;
        ent->pos[0] = pivot->x + nx;
        ent->pos[1] = pivot->y + ny;
        ent->pos[2] = pivot->z + nz;
    }
}
