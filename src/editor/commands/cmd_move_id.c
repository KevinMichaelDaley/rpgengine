/**
 * @file cmd_move_id.c
 * @brief Move by ID — translates a specific entity without requiring selection.
 *
 * JSON args: {"entity_id": N, "delta": [dx, dy, dz]}
 * Fails if entity does not exist or delta is invalid.
 * Records undo entry with inverse delta.
 * Calls bridge on_move if configured.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_undo.h"

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

bool cmd_move_id(edit_dispatch_t *d, const json_value_t *args,
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

    /* Extract delta vector. */
    const json_value_t *delta_val = json_object_get(args, "delta");
    float delta[3];
    if (!extract_vec3_(delta_val, delta)) return false;

    /* Validate entity exists. */
    edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, eid);
    if (!e) return false;

    /* Apply delta. */
    e->pos[0] += delta[0];
    e->pos[1] += delta[1];
    e->pos[2] += delta[2];

    /* Bridge: notify physics engine of new position. */
    if (ctx->bridge && ctx->bridge->on_move) {
        ctx->bridge->on_move(ctx->bridge->user_data, eid,
                             e->body_index, e->pos);
    }

    /* Record undo with inverse delta. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_MOVE;
        entry.inverse_type = EDIT_CMD_TYPE_MOVE;
        entry.entity_id    = eid;
        entry.delta[0]     = -delta[0];
        entry.delta[1]     = -delta[1];
        entry.delta[2]     = -delta[2];
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    return true;
}
