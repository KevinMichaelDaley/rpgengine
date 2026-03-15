/**
 * @file cmd_rotate.c
 * @brief Rotate command — rotates selected entities by euler angle delta.
 *
 * JSON args: {"delta":[rx, ry, rz]} (degrees)
 * Records per-entity undo entries with the inverse (negated) delta.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/math/quat.h"

/**
 * @brief Extract a 3-element float array from a JSON array value.
 */
static bool extract_vec3_(const json_value_t *arr, float out[3]) {
    if (!arr || arr->type != JSON_ARRAY || arr->array.count < 3) return false;
    for (int i = 0; i < 3; i++) {
        if (arr->array.items[i].type != JSON_NUMBER) return false;
        out[i] = (float)arr->array.items[i].number;
    }
    return true;
}

bool cmd_rotate(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection || !args) return false;

    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) return true;

    const uint32_t *ids = edit_selection_ids(ctx->selection);

    /* Accept:
     *   "abs":[x,y,z,w]   — set absolute orientation (preferred for snap)
     *   "quat":[x,y,z,w]  — incremental rotation quaternion
     *   "delta":[rx,ry,rz] — legacy euler angles in degrees (TUI) */
    bool is_absolute = false;
    quat_t abs_orient = {0, 0, 0, 1};
    quat_t dq = {0, 0, 0, 1};

    const json_value_t *abs_val = json_object_get(args, "abs");
    if (abs_val && abs_val->type == JSON_ARRAY && abs_val->array.count >= 4) {
        is_absolute = true;
        abs_orient.x = (float)abs_val->array.items[0].number;
        abs_orient.y = (float)abs_val->array.items[1].number;
        abs_orient.z = (float)abs_val->array.items[2].number;
        abs_orient.w = (float)abs_val->array.items[3].number;
        abs_orient = quat_normalize_safe(abs_orient, 1e-8f);
    } else {
        const json_value_t *quat_val = json_object_get(args, "quat");
        if (quat_val && quat_val->type == JSON_ARRAY &&
            quat_val->array.count >= 4) {
            dq.x = (float)quat_val->array.items[0].number;
            dq.y = (float)quat_val->array.items[1].number;
            dq.z = (float)quat_val->array.items[2].number;
            dq.w = (float)quat_val->array.items[3].number;
            dq = quat_normalize_safe(dq, 1e-8f);
        } else {
            const json_value_t *delta_val = json_object_get(args, "delta");
            float delta[3];
            if (!extract_vec3_(delta_val, delta)) return false;
            static const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
            dq = quat_from_euler_yxz(
                delta[0] * DEG_TO_RAD, delta[1] * DEG_TO_RAD,
                delta[2] * DEG_TO_RAD);
        }
    }

    if (ctx->undo && count > 1) {
        edit_undo_begin_group(ctx->undo);
    }

    for (uint32_t i = 0; i < count; i++) {
        edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, ids[i]);
        if (!e) continue;

        /* Save old orientation for undo. */
        quat_t old_orient = e->orientation;

        if (is_absolute) {
            /* Set orientation directly (used after client-side snap). */
            e->orientation = abs_orient;
        } else {
            /* Compose incremental quaternion rotation. */
            e->orientation = quat_normalize_safe(
                quat_mul(dq, e->orientation), 1e-8f);
        }

        /* Sync euler cache for display/serialization.
         * Canonicalize w >= 0 for consistent euler branches. */
        {
            quat_t cq = e->orientation;
            if (cq.w < 0.0f) { cq.x = -cq.x; cq.y = -cq.y; cq.z = -cq.z; cq.w = -cq.w; }
            quat_to_euler_yxz(cq, &e->rot[0], &e->rot[1], &e->rot[2]);
        }
        float rad_to_deg = 180.0f / 3.14159265358979323846f;
        e->rot[0] *= rad_to_deg;
        e->rot[1] *= rad_to_deg;
        e->rot[2] *= rad_to_deg;

        /* Bridge: notify physics of new orientation (body position may
         * change if entity has non-zero pivot_offset). */
        if (ctx->bridge && ctx->bridge->on_move) {
            ctx->bridge->on_move(ctx->bridge->user_data, ids[i],
                                 e->body_index, e);
        }

        /* Version stamp the rotated entity. */
        if (ctx->version) edit_version_stamp(ctx->version, ids[i]);

        /* Record undo: inverse is delta from new back to old. */
        if (ctx->undo) {
            quat_t inv_dq = quat_normalize_safe(
                quat_mul(old_orient, quat_conjugate(e->orientation)), 1e-8f);
            edit_undo_entry_t entry = {0};
            entry.forward_type = EDIT_CMD_TYPE_ROTATE;
            entry.inverse_type = EDIT_CMD_TYPE_ROTATE;
            entry.entity_id    = ids[i];
            entry.delta[0]     = inv_dq.x;
            entry.delta[1]     = inv_dq.y;
            entry.delta[2]     = inv_dq.z;
            entry.delta[3]     = inv_dq.w;
            edit_undo_record(ctx->undo, &entry, NULL, 0);
        }
    }

    if (ctx->undo && count > 1) {
        edit_undo_end_group(ctx->undo);
    }

    return true;
}
