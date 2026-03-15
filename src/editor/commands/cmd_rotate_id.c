/**
 * @file cmd_rotate_id.c
 * @brief Rotate by ID — rotates a specific entity without requiring selection.
 *
 * JSON args: {"entity_id": N_or_"name", "delta": [rx, ry, rz]}
 * Fails if entity does not exist or delta is invalid.
 * Records undo entry with inverse delta.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
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

bool cmd_rotate_id(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract entity_id (required) — accepts number or name string. */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val) return false;
    uint32_t eid = edit_cmd_resolve_entity(ctx, id_val);
    if (eid == EDIT_ENTITY_INVALID_ID) return false;

    /* Accept "abs":[x,y,z,w] (set absolute quaternion) or
     * "delta":[rx,ry,rz] (euler degrees). */
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
        const json_value_t *delta_val = json_object_get(args, "delta");
        float delta[3];
        if (!extract_vec3_(delta_val, delta)) return false;
        static const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        dq = quat_from_euler_yxz(
            delta[0] * DEG_TO_RAD, delta[1] * DEG_TO_RAD,
            delta[2] * DEG_TO_RAD);
    }

    /* Validate entity exists. */
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    quat_t old_orient = e->orientation;

    if (is_absolute) {
        e->orientation = abs_orient;
    } else {
        e->orientation = quat_normalize_safe(
            quat_mul(dq, e->orientation), 1e-8f);
    }

    /* Sync euler cache (canonicalize w >= 0 for consistent branches). */
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
        ctx->bridge->on_move(ctx->bridge->user_data, eid,
                             e->body_index, e);
    }

    /* Version stamp the rotated entity. */
    if (ctx->version) edit_version_stamp(ctx->version, eid);

    /* Record undo: delta from new back to old. */
    if (ctx->undo) {
        quat_t inv_dq = quat_normalize_safe(
            quat_mul(old_orient, quat_conjugate(e->orientation)), 1e-8f);
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_ROTATE;
        entry.inverse_type = EDIT_CMD_TYPE_ROTATE;
        entry.entity_id    = eid;
        entry.delta[0]     = inv_dq.x;
        entry.delta[1]     = inv_dq.y;
        entry.delta[2]     = inv_dq.z;
        entry.delta[3]     = inv_dq.w;
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    return true;
}
