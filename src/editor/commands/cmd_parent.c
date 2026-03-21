/**
 * @file cmd_parent.c
 * @brief Parent command — attach child entity under parent in LCRS tree.
 *
 * JSON args: {"child": <id_or_name>, "parent": <id_or_name>}
 * Records undo entry (EDIT_CMD_TYPE_MOVE used as reparent marker).
 *
 * Non-static functions (1 / 4 limit):
 *   cmd_parent
 */

#include "ferrum/editor/edit_commands.h"
#include "ferrum/editor/edit_cmd_ctx.h"
#include "ferrum/editor/edit_entity.h"
#include "ferrum/editor/edit_scene_tree.h"
#include "ferrum/entity/entity_attrs.h"

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

    if (!edit_scene_tree_attach(tree, child_id, parent_id)) {
        return false; /* Self or circular. */
    }

    /* Store parent_id as attribute so it syncs and serializes. */
    edit_entity_t *child = edit_entity_store_get_mut(ctx->entities, child_id);
    if (child) {
        entity_attrs_set(&child->attrs, SCRIPT_KEY_PARENT_ID,
                          SCRIPT_ATTR_U32, &parent_id, sizeof(parent_id));
    }

    result->type   = JSON_BOOL;
    result->boolean = true;
    return true;
}
