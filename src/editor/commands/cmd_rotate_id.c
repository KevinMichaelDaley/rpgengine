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

    /* Extract delta rotation. */
    const json_value_t *delta_val = json_object_get(args, "delta");
    float delta[3];
    if (!extract_vec3_(delta_val, delta)) return false;

    /* Validate entity exists. */
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Build incremental rotation quaternion in YXZ order. */
    static const float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    quat_t dq = quat_from_euler_yxz(
        delta[0] * DEG_TO_RAD, delta[1] * DEG_TO_RAD, delta[2] * DEG_TO_RAD);

    /* Compose quaternion rotation. */
    e->orientation = quat_normalize_safe(
        quat_mul(dq, e->orientation), 1e-8f);

    /* Sync euler cache. */
    quat_to_euler_yxz(e->orientation, &e->rot[0], &e->rot[1], &e->rot[2]);
    float rad_to_deg = 180.0f / 3.14159265358979323846f;
    e->rot[0] *= rad_to_deg;
    e->rot[1] *= rad_to_deg;
    e->rot[2] *= rad_to_deg;

    /* Record undo with inverse quaternion. */
    if (ctx->undo) {
        quat_t inv_dq = quat_conjugate(dq);
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
