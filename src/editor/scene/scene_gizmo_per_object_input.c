/**
 * @file scene_gizmo_per_object_input.c
 * @brief Per-object gizmo input handling: hit test, drag, and command sending.
 *
 * Provides functions called from scene_input.c when per-object gizmo
 * mode is active. Each function handles one phase of the gizmo
 * interaction for a single entity rather than the entire selection.
 *
 * Non-static functions (3 / 4 limit):
 *   scene_per_object_gizmo_hit_test
 *   scene_per_object_send_commands
 *   scene_per_object_send_abs_command
 */

#include "ferrum/editor/scene/scene_gizmo_per_object.h"
#include "ferrum/editor/scene/scene_main.h"
#include "ferrum/editor/scene/scene_cmd.h"
#include "ferrum/editor/scene/scene_connection.h"
#include "ferrum/editor/scene/scene_sync.h"
#include "ferrum/editor/scene/viewport_bsp/viewport_state.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/viewport/viewport_gizmo.h"
#include "ferrum/editor/viewport/viewport_camera.h"
#include "ferrum/editor/viewport/transform_basis.h"
#include "ferrum/math/mat4.h"
#include "ferrum/math/vec3.h"
#include "ferrum/math/quat.h"

#include <stdio.h>
#include <math.h>

/** Maximum per-object gizmos for hit testing (stack allocation). */
#define PER_OBJECT_HIT_MAX 64

bool scene_per_object_gizmo_hit_test(
    scene_editor_t *ed,
    const editor_ray_t *ray,
    float gizmo_scale,
    const mat4_t *vp_matrix,
    float screen_x,
    float screen_y)
{
    if (!ed || !ray || !vp_matrix) return false;

    viewport_state_t *fvp = scene_focused_vp(ed);
    vec3_t eye = editor_camera_eye_position(&fvp->camera);

    per_object_gizmo_t gizmos[PER_OBJECT_HIT_MAX];
    uint32_t count = per_object_gizmo_build(
        &ed->entities, &ed->selection,
        fvp->gizmo.mode, fvp->gizmo.basis,
        vp_matrix, &eye,
        gizmos, PER_OBJECT_HIT_MAX);

    if (count == 0) return false;

    gizmo_axis_t hit_axis;
    uint32_t picked_entity = per_object_gizmo_pick(
        gizmos, count, ray, gizmo_scale,
        vp_matrix, screen_x, screen_y, &hit_axis);

    if (picked_entity == EDIT_ENTITY_INVALID_ID) return false;

    /* Set up drag state. */
    fvp->per_object_drag_entity = picked_entity;
    fvp->gizmo.active_axis = hit_axis;
    fvp->gizmo.dragging = true;
    fvp->gizmo_drag_accum = (vec3_t){0, 0, 0};
    fvp->gizmo_scale_accum = (vec3_t){1, 1, 1};
    fvp->gizmo_rot_accum = (quat_t){0, 0, 0, 1};
    fvp->snap_applied_delta = (vec3_t){0, 0, 0};
    fvp->snap_applied_scale = (vec3_t){1, 1, 1};
    fvp->snap_rot_accum_deg = 0.0f;
    fvp->snap_rot_applied_deg = 0.0f;
    fvp->snap_rot_origin[0] = 0.0f;
    fvp->snap_rot_origin[1] = 0.0f;
    fvp->snap_rot_origin[2] = 0.0f;

    /* Set gizmo position to the picked entity's pos for drag computation. */
    const edit_entity_t *ent = edit_entity_store_get(
        &ed->entities, picked_entity);
    if (ent) {
        fvp->gizmo.position = (vec3_t){
            ent->pos[0], ent->pos[1], ent->pos[2]};
        fvp->gizmo_drag_origin = fvp->gizmo.position;
        fvp->snap_origin_pos = fvp->gizmo.position;
        fvp->snap_origin_scale = (vec3_t){
            ent->scale[0], ent->scale[1], ent->scale[2]};

        /* Set orientation to picked entity's orientation for local basis. */
        transform_basis_t effective = fvp->gizmo.basis;
        if (fvp->gizmo.mode == GIZMO_MODE_SCALE) {
            effective = TRANSFORM_BASIS_LOCAL;
        }
        const quat_t *orient = NULL;
        if (effective == TRANSFORM_BASIS_LOCAL) {
            orient = &ent->orientation;
        }
        mat4_t view;
        editor_camera_view_matrix(&fvp->camera, &view);
        fvp->gizmo.orientation = transform_basis_orientation(
            effective, orient, &view);

        fvp->snap_rot_origin[0] = ent->rot[0];
        fvp->snap_rot_origin[1] = ent->rot[1];
        fvp->snap_rot_origin[2] = ent->rot[2];
    }

    return true;
}

void scene_per_object_send_commands(
    scene_editor_t *ed,
    vec3_t total_delta)
{
    if (!ed) return;

    viewport_state_t *fvp = scene_focused_vp(ed);
    uint32_t entity_id = fvp->per_object_drag_entity;
    if (entity_id == EDIT_ENTITY_INVALID_ID) return;

    const edit_entity_t *ent = edit_entity_store_get(
        &ed->entities, entity_id);
    if (!ent) return;

    char buf[256];
    uint32_t cid = scene_connection_next_id(&ed->connection);
    int n = 0;

    switch (fvp->gizmo.mode) {
    case GIZMO_MODE_TRANSLATE:
        n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
            "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
            (unsigned)cid, (unsigned)entity_id,
            (double)ent->pos[0], (double)ent->pos[1],
            (double)ent->pos[2]);
        break;
    case GIZMO_MODE_ROTATE:
        n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"rotate_id\",\"args\":"
            "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g,%.8g]}}\n",
            (unsigned)cid, (unsigned)entity_id,
            (double)ent->orientation.x, (double)ent->orientation.y,
            (double)ent->orientation.z, (double)ent->orientation.w);
        if (n > 0 && (size_t)n < sizeof(buf)) {
            scene_connection_send_cmd(&ed->connection, buf);
            scene_sync_mark_sent(&ed->sync);
        }
        /* In cursor basis, rotation also orbits position around the
         * cursor. Send a move_id so the server has the new position. */
        if (fvp->gizmo.basis == TRANSFORM_BASIS_CURSOR) {
            cid = scene_connection_next_id(&ed->connection);
            n = snprintf(buf, sizeof(buf),
                "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
                "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
                (unsigned)cid, (unsigned)entity_id,
                (double)ent->pos[0], (double)ent->pos[1],
                (double)ent->pos[2]);
        }
        break;
    case GIZMO_MODE_SCALE:
        n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"scale_id\",\"args\":"
            "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
            (unsigned)cid, (unsigned)entity_id,
            (double)ent->scale[0], (double)ent->scale[1],
            (double)ent->scale[2]);
        break;
    default:
        break;
    }

    if (n > 0 && (size_t)n < sizeof(buf)) {
        scene_connection_send_cmd(&ed->connection, buf);
        scene_sync_mark_sent(&ed->sync);
    }
}

void scene_per_object_send_abs_command(
    scene_editor_t *ed,
    uint32_t entity_id,
    gizmo_mode_t mode)
{
    if (!ed) return;

    const edit_entity_t *ent = edit_entity_store_get(
        &ed->entities, entity_id);
    if (!ent) return;

    char buf[256];
    uint32_t cid = scene_connection_next_id(&ed->connection);
    int n = 0;

    switch (mode) {
    case GIZMO_MODE_TRANSLATE:
        n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"move_id\",\"args\":"
            "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
            (unsigned)cid, (unsigned)entity_id,
            (double)ent->pos[0], (double)ent->pos[1],
            (double)ent->pos[2]);
        break;
    case GIZMO_MODE_ROTATE:
        n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"rotate_id\",\"args\":"
            "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g,%.8g]}}\n",
            (unsigned)cid, (unsigned)entity_id,
            (double)ent->orientation.x, (double)ent->orientation.y,
            (double)ent->orientation.z, (double)ent->orientation.w);
        break;
    case GIZMO_MODE_SCALE:
        n = snprintf(buf, sizeof(buf),
            "{\"id\":%u,\"cmd\":\"scale_id\",\"args\":"
            "{\"entity_id\":%u,\"abs\":[%.8g,%.8g,%.8g]}}\n",
            (unsigned)cid, (unsigned)entity_id,
            (double)ent->scale[0], (double)ent->scale[1],
            (double)ent->scale[2]);
        break;
    default:
        break;
    }

    if (n > 0 && (size_t)n < sizeof(buf)) {
        scene_connection_send_cmd(&ed->connection, buf);
        scene_sync_mark_sent(&ed->sync);
    }
}
