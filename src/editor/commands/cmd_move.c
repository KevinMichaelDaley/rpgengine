/**
 * @file cmd_move.c
 * @brief Move command — translates selected entities by a delta vector.
 *
 * JSON args: {"delta":[dx, dy, dz]}
 * Operates on the current selection. Records per-entity undo entries
 * with the inverse (negated) delta.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
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

bool cmd_move(edit_dispatch_t *d, const json_value_t *args,
              json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection || !args) return false;

    /* Extract delta vector. */
    const json_value_t *delta_val = json_object_get(args, "delta");
    float delta[3];
    if (!extract_vec3_(delta_val, delta)) return false;

    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) return true;

    const uint32_t *ids = edit_selection_ids(ctx->selection);

    /* Group undo entries for multi-entity moves. */
    if (ctx->undo && count > 1) {
        edit_undo_begin_group(ctx->undo);
    }

    for (uint32_t i = 0; i < count; i++) {
        edit_entity_t *e = edit_entity_store_get_mut(ctx->entities, ids[i]);
        if (!e) continue;

        /* Apply delta. */
        e->pos[0] += delta[0];
        e->pos[1] += delta[1];
        e->pos[2] += delta[2];

        /* Record undo with inverse delta. */
        if (ctx->undo) {
            edit_undo_entry_t entry = {0};
            entry.forward_type = EDIT_CMD_TYPE_MOVE;
            entry.inverse_type = EDIT_CMD_TYPE_MOVE;
            entry.entity_id    = ids[i];
            entry.delta[0]     = -delta[0];
            entry.delta[1]     = -delta[1];
            entry.delta[2]     = -delta[2];
            edit_undo_record(ctx->undo, &entry, NULL, 0);
        }
    }

    if (ctx->undo && count > 1) {
        edit_undo_end_group(ctx->undo);
    }

    return true;
}
