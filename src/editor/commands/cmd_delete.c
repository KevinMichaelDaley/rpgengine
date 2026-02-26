/**
 * @file cmd_delete.c
 * @brief Delete command — removes all selected entities.
 *
 * Captures full entity snapshots for undo. Clears selection after delete.
 * JSON args: {} (operates on selection set)
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_selection.h"
#include "ferrum/editor/edit_undo.h"

bool cmd_delete(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    (void)args;
    (void)result;
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !ctx->selection) return false;

    uint32_t count = edit_selection_count(ctx->selection);
    if (count == 0) return true; /* No-op, still success. */

    const uint32_t *ids = edit_selection_ids(ctx->selection);

    /* Group undo entries for multi-delete. */
    if (ctx->undo && count > 1) {
        edit_undo_begin_group(ctx->undo);
    }

    /* Delete each selected entity, recording snapshots. */
    for (uint32_t i = 0; i < count; i++) {
        uint32_t eid = ids[i];
        const edit_entity_t *e = edit_entity_store_get(ctx->entities, eid);
        if (!e) continue;

        /* Record undo entry with snapshot. */
        if (ctx->undo) {
            edit_undo_entry_t entry = {0};
            entry.forward_type = EDIT_CMD_TYPE_DELETE;
            entry.inverse_type = EDIT_CMD_TYPE_SPAWN;
            entry.entity_id    = eid;
            edit_undo_record(ctx->undo, &entry, e, sizeof(edit_entity_t));
        }

        edit_entity_store_remove(ctx->entities, eid);
    }

    if (ctx->undo && count > 1) {
        edit_undo_end_group(ctx->undo);
    }

    /* Clear selection after deleting entities. */
    edit_selection_clear(ctx->selection);
    return true;
}
