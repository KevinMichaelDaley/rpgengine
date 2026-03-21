/**
 * @file cmd_parent.c
 * @brief Parent command — attach child entity under parent in LCRS tree.
 *
 * JSON args: {"child": <id_or_name>, "parent": <id_or_name>}
 * Records undo entry with old parent stored in delta[0].
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_parent
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_scene_tree.h"
#include "ferrum/editor/edit_undo.h"
#include "ferrum/editor/edit_entity_version.h"
#include "ferrum/entity/entity_attrs.h"

#include <string.h>

bool cmd_parent(edit_dispatch_t *d, const json_value_t *args,
                json_value_t *result, json_arena_t *arena) {
    (void)arena;
    edit_cmd_ctx_t *ctx = (edit_cmd_ctx_t *)d->user_data;
    if (!ctx || !ctx->entities || !args) return false;

    const json_value_t *child_val  = json_object_get(args, "child");
    const json_value_t *parent_val = json_object_get(args, "parent");
    if (!child_val || !parent_val) return false;

    uint32_t child_id  = edit_cmd_resolve_entity(ctx, child_val);
    uint32_t parent_id = edit_cmd_resolve_entity(ctx, parent_val);
    if (child_id == EDIT_ENTITY_INVALID_ID ||
        parent_id == EDIT_ENTITY_INVALID_ID) {
        return false;
    }

    edit_scene_tree_t *tree = ctx->entities->tree;
    if (!tree) return false;

    /* Save old parent for undo. */
    uint32_t old_parent = edit_scene_tree_get_parent(tree, child_id);

    if (!edit_scene_tree_attach(tree, child_id, parent_id)) {
        return false; /* Self or circular. */
    }

    /* Store parent_id as attribute so it syncs and serializes. */
    edit_entity_t *child = edit_entity_store_get_mut(ctx->entities, child_id);
    if (child) {
        entity_attrs_set(&child->attrs, SCRIPT_KEY_PARENT_ID,
                          SCRIPT_ATTR_U32, &parent_id, sizeof(parent_id));
    }

    /* Version stamp so sync picks up the change. */
    if (ctx->version) edit_version_stamp(ctx->version, child_id);

    /* Record undo: old parent stored in delta[0] as float-encoded uint32. */
    if (ctx->undo) {
        edit_undo_entry_t entry = {0};
        entry.forward_type = EDIT_CMD_TYPE_REPARENT;
        entry.inverse_type = EDIT_CMD_TYPE_REPARENT;
        entry.entity_id    = child_id;
        /* Store old parent as uint32 bits in delta[0]. */
        memcpy(&entry.delta[0], &old_parent, sizeof(uint32_t));
        /* Store new parent in delta[1] for redo. */
        memcpy(&entry.delta[1], &parent_id, sizeof(uint32_t));
        edit_undo_record(ctx->undo, &entry, NULL, 0);
    }

    result->type    = JSON_BOOL;
    result->boolean = true;
    return true;
}
