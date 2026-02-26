/**
 * @file cmd_delete_id.c
 * @brief Delete by ID — removes a specific entity without requiring selection.
 *
 * JSON args: {"entity_id": N}
 * Fails if entity does not exist.
 * Records undo entry with entity snapshot.
 * Calls bridge on_delete if configured.
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"

bool cmd_delete_id(edit_dispatch_t *d, const json_value_t *args,
                   json_value_t *result, json_arena_t *arena) {
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    /* Extract entity_id (required). */
    const json_value_t *id_val = json_object_get(args, "entity_id");
    if (!id_val || id_val->type != JSON_NUMBER) return false;
    uint32_t eid = (uint32_t)id_val->number;

    /* Validate entity exists. */
    const edit_entity_t *e = edit_entity_store_get(ctx->entities, eid);
    if (!e) return false;

    /* Record undo with snapshot. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_DELETE;
        entry.inverse_type = EDIT_CMD_TYPE_SPAWN;
        entry.entity_id    = eid;
        edit_undo_record(ctx->undo, &entry, e, sizeof(edit_entity_t));
    }

    /* Bridge: notify physics engine before removing. */
    if (ctx->bridge && ctx->bridge->on_delete) {
        ctx->bridge->on_delete(ctx->bridge->user_data, eid, e->body_index);
    }

    /* Remove from selection if present. */
    if (ctx->selection) {
        edit_selection_remove(ctx->selection, eid);
    }

    edit_entity_store_remove(ctx->entities, eid);
    return true;
}
